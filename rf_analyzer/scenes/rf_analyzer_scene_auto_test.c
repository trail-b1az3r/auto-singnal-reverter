#include "../rf_analyzer_i.h"
#include "../helpers/rf_analyzer_tx.h"
#include "../helpers/rf_analyzer_nrf24.h"

/*
 * Auto Inverse Test Scene — automatic inverse-signal test for ANY detected
 * frequency (authorized laboratory equipment only).
 *
 * Workflow per cycle:
 * 1. SWEEP the configured scan range (Settings → Band/Step/Dwell/RSSI/Mod).
 * 2. DETECT a signal on whatever frequency it appears.
 * 3. CAPTURE + ANALYZE its timing on that frequency.
 * 4. Check the signal format is supported (unless bypassed).
 * 5. Generate its logical waveform inverse (timing preserved).
 * 6. Transmit the inverse on the detected frequency ONLY when the
 *    configured test criteria match.
 * 7. Stop after the configured TX duration.
 * 8. Return to RX sweep and keep monitoring.
 *
 * Radio-lifecycle rules (these prevent firmware furi_check crashes):
 * - The CC1101 is held by exactly one engine at a time. The sweep is
 *   stopped before capture starts; capture is stopped before TX starts;
 *   capture restarts after TX before the next sweep-wait state.
 * - Back (navigation event) emergency-stops an active test immediately.
 * - Scene exit stops the UI timer FIRST (it dereferences scene state),
 *   then releases every radio engine.
 *
 * Safeguards (restricted mode):
 * - Disabled by default (must explicitly enable).
 * - Max TX duration and configurable cooldown.
 * - Never transmits unsupported/undecoded signals (require_decode).
 * - Physical Back key emergency stop; prominent "AUTO TX" display.
 * - Stops all transmission when the application exits.
 * - Respects firmware frequency/power restrictions.
 */

#define AUTO_TEST_UI_REFRESH_MS 100

// Auto Test state machine
typedef enum {
    AutoStateIdle = 0, // Waiting for user to start
    AutoStateRx, // Sweeping the range, watching for a signal
    AutoStateDetected, // Signal detected, parking the receiver
    AutoStateAnalyzing, // Running capture/analysis on the detected frequency
    AutoStateGenerating, // Building inverse waveform
    AutoStateTransmitting, // TX active (AUTO TX)
    AutoStateCooldown, // Enforcing cooldown period
    AutoStateError, // Error state
} AutoTestState;

// State names for display
static const char* const auto_state_names[] = {
    "IDLE",
    "RX SWEEP",
    "DETECTED",
    "ANALYZING",
    "GENERATING",
    "TRANSMITTING",
    "COOLDOWN",
    "ERROR",
};

// Context for the auto test scene
typedef struct {
    RfAnalyzerApp* app;
    AutoTestState state;
    uint32_t state_start_tick;
    RfTxEngine* tx_engine;
    RfTxWaveform waveform;
    RfCaptureStats capture_stats;
    RfSignal detected_signal;
    bool capture_running;
    bool scanning;
    volatile bool sweep_hit;
    volatile uint32_t sweep_freq;
    volatile float sweep_rssi;
    uint32_t cooldown_end_tick;
    char error_msg[64];
} AutoTestContext;

static AutoTestContext* s_ctx = NULL;

// Forward declarations
static void auto_test_build_ui(RfAnalyzerApp* app);
static void auto_test_timer_cb(void* context);
static void auto_test_start_test(AutoTestContext* ctx);
static void auto_test_stop_test(AutoTestContext* ctx);
static void auto_test_emergency_stop(AutoTestContext* ctx);
static void auto_test_state_machine(AutoTestContext* ctx);
static void auto_test_rx_stop(AutoTestContext* ctx);
static bool auto_test_rx_capture(AutoTestContext* ctx, uint32_t freq);

// Scanner callback: runs on the scanner thread. Records the FIRST detection
// of the current sweep cycle; the timer-thread state machine picks it up.
static void auto_test_scan_cb(const RfSignal* signal, void* context) {
    AutoTestContext* ctx = context;
    if(!ctx->sweep_hit) {
        ctx->sweep_freq = signal->frequency;
        ctx->sweep_rssi = signal->rssi;
        ctx->sweep_hit = true;
    }
    rf_app_add_signal(ctx->app, signal);
    notification_message(ctx->app->notifications, &sequence_blink_blue_10);
}

// Widget center-button callback. Short press toggles the test; the physical
// Back key is handled via the scene Back event so it can emergency-stop an
// active transmission first.
static void auto_test_ok_cb(GuiButtonType btn, InputType type, void* context) {
    UNUSED(btn);
    if(type != InputTypeShort) return;
    AutoTestContext* ctx = context;
    if(ctx->state == AutoStateIdle || ctx->state == AutoStateError) {
        auto_test_start_test(ctx);
    } else {
        auto_test_stop_test(ctx);
    }
}

// Timer callback for state machine and UI refresh
static void auto_test_timer_cb(void* context) {
    AutoTestContext* ctx = context;
    auto_test_state_machine(ctx);
    auto_test_build_ui(ctx->app);
}

// Stop every RX engine the test may hold (sweep and/or capture).
static void auto_test_rx_stop(AutoTestContext* ctx) {
    RfAnalyzerApp* app = ctx->app;
    if(ctx->scanning) {
        rf_scanner_stop(app->scanner);
        ctx->scanning = false;
    }
    if(ctx->capture_running) {
        rf_capture_stop(app->capture);
        ctx->capture_running = false;
    }
}

// Park the receiver on freq and start timing analysis. The sweep must already
// be stopped: the radio can only be held by one engine at a time.
static bool auto_test_rx_capture(AutoTestContext* ctx, uint32_t freq) {
    if(ctx->capture_running) {
        rf_capture_stop(ctx->app->capture);
        ctx->capture_running = false;
    }
    if(rf_capture_start(ctx->app->capture, freq, ctx->app->config.preset)) {
        ctx->capture_running = true;
        return true;
    }
    return false;
}

// Start the auto test
static void auto_test_start_test(AutoTestContext* ctx) {
    RfAnalyzerApp* app = ctx->app;
    const RfAutoTestConfig* config = &app->auto_test_config;

    // Clean slate in case a previous run left engines behind.
    auto_test_rx_stop(ctx);
    if(ctx->tx_engine) {
        rf_tx_engine_free(ctx->tx_engine);
        ctx->tx_engine = NULL;
    }

    // Master enable gate always applies.
    if(!config->enabled) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Auto Test not enabled in settings");
        ctx->state = AutoStateError;
        return;
    }

    // NRF24 has no HAL in the official SDK: fail fast with a clear message
    // instead of sweeping for a signal we could never answer.
    if(config->nrf24_mode) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "NRF24 needs ext module driver");
        ctx->state = AutoStateError;
        return;
    }

    // The sweep range comes from the main scan configuration.
    const char* scan_err = rf_scan_config_validate(&app->config);
    if(scan_err) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Bad scan range: %s", scan_err);
        ctx->state = AutoStateError;
        return;
    }

    // Validate TX duration / cooldown entry gates (bypass mode skips them;
    // per-burst firmware checks still apply at TX time).
    if(!config->remove_all_restrictions) {
        if(config->tx_duration_ms < RF_AUTO_TEST_MIN_DURATION_MS ||
           config->tx_duration_ms > RF_AUTO_TEST_MAX_DURATION_MS) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Invalid TX duration");
            ctx->state = AutoStateError;
            return;
        }
        if(!config->remove_cooldown_limit && config->cooldown_ms > RF_AUTO_TEST_MAX_COOLDOWN_MS) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Cooldown too long (max 60s)");
            ctx->state = AutoStateError;
            return;
        }
    }

    ctx->tx_engine = rf_tx_engine_alloc();
    if(!ctx->tx_engine) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Failed to allocate TX engine");
        ctx->state = AutoStateError;
        return;
    }

    // Clear state
    ctx->state = AutoStateRx;
    ctx->state_start_tick = furi_get_tick();
    ctx->sweep_hit = false;
    ctx->sweep_freq = 0;
    ctx->cooldown_end_tick = 0;
    memset(&ctx->waveform, 0, sizeof(ctx->waveform));
    memset(&ctx->detected_signal, 0, sizeof(ctx->detected_signal));
    memset(ctx->error_msg, 0, sizeof(ctx->error_msg));

    // Start sweeping the configured range for ANY signal.
    rf_scanner_set_callback(app->scanner, auto_test_scan_cb, ctx);
    if(!rf_scanner_start(app->scanner, &app->config)) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Failed to start sweep");
        rf_app_scanner_restore_callback(app);
        rf_tx_engine_free(ctx->tx_engine);
        ctx->tx_engine = NULL;
        ctx->state = AutoStateError;
        return;
    }
    ctx->scanning = true;
}

// Stop the auto test (normal stop)
static void auto_test_stop_test(AutoTestContext* ctx) {
    RfAnalyzerApp* app = ctx->app;

    // Stop any ongoing TX
    if(ctx->tx_engine) {
        rf_tx_emergency_stop(ctx->tx_engine);
        rf_tx_engine_free(ctx->tx_engine);
        ctx->tx_engine = NULL;
    }

    // Release every radio engine and give the scanner back to Scan scene.
    auto_test_rx_stop(ctx);
    rf_app_scanner_restore_callback(app);

    ctx->state = AutoStateIdle;
    ctx->sweep_hit = false;
}

// Emergency stop - immediate halt of all radio activity
static void auto_test_emergency_stop(AutoTestContext* ctx) {
    RfAnalyzerApp* app = ctx->app;

    if(ctx->tx_engine) {
        rf_tx_emergency_stop(ctx->tx_engine);
    }
    auto_test_rx_stop(ctx);
    rf_app_scanner_restore_callback(app);

    ctx->state = AutoStateIdle;
    ctx->sweep_hit = false;
    snprintf(ctx->error_msg, sizeof(ctx->error_msg), "EMERGENCY STOP");
}

// Main state machine
static void auto_test_state_machine(AutoTestContext* ctx) {
    RfAnalyzerApp* app = ctx->app;
    const RfAutoTestConfig* config = &app->auto_test_config;
    uint32_t now = furi_get_tick();

    switch(ctx->state) {
    case AutoStateIdle:
    case AutoStateError:
        // Waiting for user to press Start
        break;

    case AutoStateRx: {
        // A sweep hit hands us the exact frequency to engage.
        if(ctx->sweep_hit) {
            uint32_t freq = ctx->sweep_freq;
            float rssi = ctx->sweep_rssi;
            ctx->sweep_hit = false;

            // The sweep holds the radio: release it before capturing.
            if(ctx->scanning) {
                rf_scanner_stop(app->scanner);
                ctx->scanning = false;
            }

            ctx->detected_signal.frequency = freq;
            ctx->detected_signal.preset = app->config.preset;
            ctx->detected_signal.duration_ms = 0;
            ctx->detected_signal.rssi = rssi;

            if(!auto_test_rx_capture(ctx, freq)) {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Capture failed, resuming");
                ctx->state = AutoStateError;
                break;
            }
            ctx->state = AutoStateDetected;
            ctx->state_start_tick = now;
        }
        break;
    }

    case AutoStateDetected: {
        // Quick validation before analysis. Bypass mode skips the decode gate
        // and engages any detected energy.
        if(!config->remove_all_restrictions && config->require_decode) {
            rf_capture_get_stats(app->capture, &ctx->capture_stats);
            if(ctx->capture_stats.est_bitrate == 0) {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Signal not decodable");
                ctx->state = AutoStateError;
                break;
            }
        }
        ctx->state = AutoStateAnalyzing;
        ctx->state_start_tick = now;
        break;
    }

    case AutoStateAnalyzing: {
        // Run capture a bit longer to get stable stats
        if((now - ctx->state_start_tick) > furi_ms_to_ticks(200)) {
            rf_capture_get_stats(app->capture, &ctx->capture_stats);
            ctx->state = AutoStateGenerating;
            ctx->state_start_tick = now;
        }
        break;
    }

    case AutoStateGenerating: {
        // Generate inverse waveform on the DETECTED frequency.
        RfInvertResult res;
        if(config->remove_all_restrictions) {
            // Bypass: fixed OOK test pattern on the detected frequency.
            ctx->waveform.modulation = RfModOOK;
            ctx->waveform.edge_count = 4;
            ctx->waveform.edges[0].level = true;
            ctx->waveform.edges[0].duration_us = 500;
            ctx->waveform.edges[1].level = false;
            ctx->waveform.edges[1].duration_us = 500;
            ctx->waveform.edges[2].level = true;
            ctx->waveform.edges[2].duration_us = 500;
            ctx->waveform.edges[3].level = false;
            ctx->waveform.edges[3].duration_us = 500;
            ctx->waveform.total_duration_us = 2000;
            ctx->waveform.bitrate = 500;
            ctx->waveform.frequency = ctx->detected_signal.frequency;
            ctx->waveform.valid = true;
            res = RfInvertOk;
        } else {
            res =
                rf_tx_generate_inverse(&ctx->capture_stats, &ctx->detected_signal, &ctx->waveform);
        }
        if(res != RfInvertOk) {
            switch(res) {
            case RfInvertErrUnsupportedModulation:
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Modulation not supported");
                break;
            case RfInvertErrNoSignal:
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Insufficient signal data");
                break;
            case RfInvertErrTooComplex:
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Signal too complex");
                break;
            case RfInvertErrTxNotAllowed:
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Freq not allowed by FW");
                break;
            default:
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Inverse failed (%d)", res);
            }
            ctx->state = AutoStateError;
            break;
        }

        // Check cooldown
        if(now < ctx->cooldown_end_tick) {
            ctx->state = AutoStateCooldown;
            break;
        }

        ctx->state = AutoStateTransmitting;
        ctx->state_start_tick = now;
        break;
    }

    case AutoStateTransmitting: {
        // CRITICAL: capture holds the radio — release it BEFORE acquiring
        // the radio for TX, or the firmware hits a furi_check and crashes.
        if(ctx->capture_running) {
            rf_capture_stop(app->capture);
            ctx->capture_running = false;
        }

        // Transmit the inverse on the detected frequency.
        RfInvertResult res;
        if(config->remove_all_restrictions) {
            res = rf_tx_transmit_waveform(
                ctx->tx_engine, &ctx->waveform, RF_AUTO_TEST_MAX_DURATION_MS);
        } else {
            res = rf_tx_transmit_waveform(ctx->tx_engine, &ctx->waveform, config->tx_duration_ms);
        }

        if(res != RfInvertOk) {
            switch(res) {
            case RfInvertErrTxNotAllowed:
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "TX blocked by FW/HW");
                break;
            case RfInvertErrUnsupportedModulation:
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "NRF24 driver missing");
                break;
            case RfInvertErrHardware:
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Hardware TX error");
                break;
            default:
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "TX failed (%d)", res);
            }
            ctx->state = AutoStateError;
        } else if(config->remove_all_restrictions) {
            // Bypass: skip cooldown, go idle for immediate re-arm.
            ctx->state = AutoStateIdle;
        } else {
            // Re-arm the receiver, then enforce cooldown before next sweep.
            auto_test_rx_capture(ctx, ctx->detected_signal.frequency);
            ctx->cooldown_end_tick = now + furi_ms_to_ticks(config->cooldown_ms);
            ctx->state = AutoStateCooldown;
        }
        break;
    }

    case AutoStateCooldown: {
        // Wait for cooldown to expire, then resume sweeping the range.
        if(now >= ctx->cooldown_end_tick) {
            auto_test_rx_stop(ctx);
            ctx->sweep_hit = false;
            rf_scanner_set_callback(app->scanner, auto_test_scan_cb, ctx);
            if(rf_scanner_start(app->scanner, &app->config)) {
                ctx->scanning = true;
                ctx->state = AutoStateRx;
                ctx->state_start_tick = now;
            } else {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Sweep restart failed");
                ctx->state = AutoStateError;
            }
        }
        break;
    }
    }
}

// Build UI for current state
static void auto_test_build_ui(RfAnalyzerApp* app) {
    Widget* w = app->widget;
    widget_reset(w);
    AutoTestContext* ctx = s_ctx;

    if(!ctx) return;

    // widget_reset() clears button elements too, so re-register on every
    // rebuild (the timer calls this ~10 Hz). Back is a navigation event,
    // handled in on_event, not as a widget button.
    widget_add_button_element(w, GuiButtonTypeCenter, "OK", auto_test_ok_cb, ctx);

    uint32_t now = furi_get_tick();

    // Header with prominent AUTO TX indicator
    if(ctx->state == AutoStateTransmitting) {
        widget_add_string_element(
            w, 64, 8, AlignCenter, AlignBottom, FontPrimary, ">>> AUTO TX <<<");
        widget_add_string_element(
            w, 64, 18, AlignCenter, AlignBottom, FontSecondary, "TRANSMITTING INVERSE");
    } else {
        widget_add_string_element(
            w, 64, 8, AlignCenter, AlignBottom, FontPrimary, "AUTO INVERSE TEST");
    }

    // Sweep range + engaged target
    char line[64];
    snprintf(
        line,
        sizeof(line),
        "Scan %lu-%luMHz",
        (unsigned long)(app->config.freq_start / 1000000),
        (unsigned long)(app->config.freq_end / 1000000));
    widget_add_string_element(w, 2, 22, AlignLeft, AlignBottom, FontSecondary, line);

    if(ctx->detected_signal.frequency) {
        uint32_t f = ctx->detected_signal.frequency;
        snprintf(
            line,
            sizeof(line),
            "Target %lu.%03lu MHz",
            (unsigned long)(f / 1000000),
            (unsigned long)((f % 1000000) / 1000));
    } else {
        snprintf(line, sizeof(line), "Mode: %s", rf_preset_name(app->config.preset));
    }
    widget_add_string_element(w, 2, 32, AlignLeft, AlignBottom, FontSecondary, line);

    // State
    snprintf(line, sizeof(line), "State: %s", auto_state_names[ctx->state]);
    widget_add_string_element(w, 2, 42, AlignLeft, AlignBottom, FontSecondary, line);

    // Status details based on state
    switch(ctx->state) {
    case AutoStateIdle:
        if(app->auto_test_config.remove_all_restrictions) {
            widget_add_string_element(
                w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "!!! RESTRICTIONS OFF !!!");
            widget_add_string_element(
                w, 2, 60, AlignLeft, AlignBottom, FontSecondary, "UNSAFE - lab only");
        } else {
            widget_add_string_element(
                w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "Press OK to start");
            widget_add_string_element(
                w, 2, 60, AlignLeft, AlignBottom, FontSecondary, "Back: Exit");
        }
        break;

    case AutoStateRx:
        widget_add_string_element(
            w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "Sweeping for signal...");
        snprintf(line, sizeof(line), "Edges n/a  Trig %d dBm", (int)app->config.rssi_trigger);
        widget_add_string_element(w, 2, 60, AlignLeft, AlignBottom, FontSecondary, line);
        break;

    case AutoStateDetected:
        widget_add_string_element(
            w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "Signal found - parking RX");
        break;

    case AutoStateAnalyzing:
        widget_add_string_element(
            w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "Analyzing signal timing...");
        break;

    case AutoStateGenerating:
        widget_add_string_element(
            w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "Generating inverse...");
        break;

    case AutoStateTransmitting:
        snprintf(
            line, sizeof(line), "TX %lu ms", (unsigned long)app->auto_test_config.tx_duration_ms);
        widget_add_string_element(w, 2, 32, AlignLeft, AlignBottom, FontSecondary, line);
        widget_add_string_element(
            w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "BACK = EMERGENCY STOP");
        break;

    case AutoStateCooldown: {
        uint32_t remaining = (ctx->cooldown_end_tick > now) ?
                                 (ctx->cooldown_end_tick - now) / furi_ms_to_ticks(1) :
                                 0;
        snprintf(line, sizeof(line), "Cooldown: %lu ms", (unsigned long)remaining);
        widget_add_string_element(w, 2, 52, AlignLeft, AlignBottom, FontSecondary, line);
        widget_add_string_element(
            w, 2, 60, AlignLeft, AlignBottom, FontSecondary, "Returning to sweep...");
        break;
    }

    case AutoStateError:
        widget_add_string_element(w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "ERROR:");
        widget_add_string_element(w, 2, 60, AlignLeft, AlignBottom, FontSecondary, ctx->error_msg);
        break;
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, RfViewWidget);
}

void rf_analyzer_scene_auto_test_on_enter(void* context) {
    RfAnalyzerApp* app = context;

    // Allocate context
    s_ctx = malloc(sizeof(AutoTestContext));
    memset(s_ctx, 0, sizeof(AutoTestContext));
    s_ctx->app = app;
    s_ctx->state = AutoStateIdle;

    // Set up widget callbacks (build_ui re-registers them after each reset)
    widget_reset(app->widget);
    widget_add_button_element(app->widget, GuiButtonTypeCenter, "OK", auto_test_ok_cb, s_ctx);

    // Start UI timer
    app->ui_timer = furi_timer_alloc(auto_test_timer_cb, FuriTimerTypePeriodic, s_ctx);
    furi_timer_start(app->ui_timer, furi_ms_to_ticks(AUTO_TEST_UI_REFRESH_MS));

    // Build initial UI
    auto_test_build_ui(app);
}

bool rf_analyzer_scene_auto_test_on_event(void* context, SceneManagerEvent event) {
    RfAnalyzerApp* app = context;
    (void)app;
    if(event.type != SceneManagerEventTypeBack) return false;

    // Physical Back key: emergency-stop an active test and stay in the scene;
    // when idle, let the scene manager pop the scene (on_exit cleans up).
    if(s_ctx && (s_ctx->state != AutoStateIdle) && (s_ctx->state != AutoStateError)) {
        auto_test_emergency_stop(s_ctx);
        auto_test_build_ui(s_ctx->app);
        return true;
    }
    return false;
}

void rf_analyzer_scene_auto_test_on_exit(void* context) {
    RfAnalyzerApp* app = context;

    // FIRST stop the UI timer: its callback dereferences s_ctx, so it must
    // never fire after the context is freed (use-after-free crash).
    if(app->ui_timer) {
        furi_timer_stop(app->ui_timer);
        furi_timer_free(app->ui_timer);
        app->ui_timer = NULL;
    }

    // Then emergency-stop everything and release the radios.
    if(s_ctx) {
        auto_test_emergency_stop(s_ctx);
        if(s_ctx->tx_engine) {
            rf_tx_engine_free(s_ctx->tx_engine);
            s_ctx->tx_engine = NULL;
        }
        free(s_ctx);
        s_ctx = NULL;
    }

    // Belt and suspenders: make sure neither engine still holds the radio.
    rf_scanner_stop(app->scanner);
    rf_capture_stop(app->capture);
    rf_app_scanner_restore_callback(app);

    widget_reset(app->widget);
}
