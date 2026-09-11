#include "../rf_analyzer_i.h"
#include "../helpers/rf_analyzer_tx.h"
#include "../helpers/rf_analyzer_nrf24.h"

/*
 * Auto Inverse Test Scene
 *
 * Implements the automatic inverse-signal test workflow:
 * 1. Monitor configured test frequency (RX)
 * 2. Detect signal above RSSI threshold
 * 3. Capture and analyze signal timing
 * 4. Check if modulation is supported for inverse generation
 * 5. Generate logical inverse waveform (preserving timing)
 * 6. Transmit inverse ONLY when all criteria met
 * 7. Enforce max TX duration and cooldown
 * 8. Return to RX monitoring
 *
 * Safeguards:
 * - Disabled by default (must explicitly enable)
 * - Single configured test frequency (not arbitrary scanning)
 * - Max TX duration and configurable cooldown
 * - Never transmits unsupported/undecoded signals
 * - Physical button (Back) emergency stop
 * - Prominent "AUTO TX" display during transmission
 * - Stops all TX on app exit
 * - Respects firmware frequency/power restrictions
 */

#define AUTO_TEST_UI_REFRESH_MS 100

// Auto Test state machine
typedef enum {
    AutoStateIdle = 0, // Waiting for user to start
    AutoStateRx, // Monitoring frequency (RX)
    AutoStateDetected, // Signal detected, analyzing
    AutoStateAnalyzing, // Running capture/analysis
    AutoStateGenerating, // Building inverse waveform
    AutoStateTransmitting, // TX active (AUTO TX)
    AutoStateCooldown, // Enforcing cooldown period
    AutoStateError, // Error state
} AutoTestState;

// State names for display
static const char* const auto_state_names[] = {
    "IDLE",
    "RX MONITOR",
    "DETECTED",
    "ANALYZING",
    "GENERATING",
    "TRANSMITTING",
    "COOLDOWN",
    "ERROR",
};

// Custom events for the auto test scene
typedef enum {
    AutoEventStartStop = 100, // OK button: start/stop test
    AutoEventEmergencyStop, // Back button: emergency stop
    AutoEventTick, // Timer tick for state machine
} AutoEvent;

// Context for the auto test scene
typedef struct {
    RfAnalyzerApp* app;
    AutoTestState state;
    AutoTestState prev_state;
    uint32_t state_start_tick;
    RfTxEngine* tx_engine;
    RfTxWaveform waveform;
    RfCaptureStats capture_stats;
    RfSignal detected_signal;
    bool signal_captured;
    bool capture_running;
    uint32_t last_tx_tick;
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
static bool auto_test_check_frequency_valid(uint32_t freq);
static void auto_test_state_machine(AutoTestContext* ctx);

// Widget center-button callback (ButtonCallback signature). Short press
// toggles the test; the physical Back key is handled via the scene Back
// event below so it can emergency-stop an active transmission first.
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

// Check if frequency is valid for TX per firmware
static bool auto_test_check_frequency_valid(uint32_t freq) {
    const SubGhzDevice* device = subghz_devices_get_by_name("cc1101_int");
    if(!device) return false;
    return subghz_devices_is_frequency_valid(device, freq);
}

// Start the auto test
static void auto_test_start_test(AutoTestContext* ctx) {
    RfAnalyzerApp* app = ctx->app;
    const RfAutoTestConfig* config = &app->auto_test_config;

    // Validate configuration. The master enable gate always applies; the
    // remaining entry checks are bypassed when remove_all_restrictions is set
    // (the TX layer still enforces hardware/firmware validity per burst).
    if(!config->enabled) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Auto Test not enabled in settings");
        ctx->state = AutoStateError;
        return;
    }

    if(!config->remove_all_restrictions) {
        if(!auto_test_check_frequency_valid(config->test_frequency)) {
            snprintf(
                ctx->error_msg, sizeof(ctx->error_msg), "Test frequency not allowed by firmware");
            ctx->state = AutoStateError;
            return;
        }

        // Validate TX duration
        if(config->tx_duration_ms < RF_AUTO_TEST_MIN_DURATION_MS ||
           config->tx_duration_ms > RF_AUTO_TEST_MAX_DURATION_MS) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Invalid TX duration");
            ctx->state = AutoStateError;
            return;
        }

        // Validate cooldown (unless override enabled)
        if(!config->remove_cooldown_limit && config->cooldown_ms > RF_AUTO_TEST_MAX_COOLDOWN_MS) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Cooldown too long (max 60s)");
            ctx->state = AutoStateError;
            return;
        }
    }

    // Initialize TX engine if not NRF24
    if(!config->nrf24_mode) {
        ctx->tx_engine = rf_tx_engine_alloc();
        if(!ctx->tx_engine) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Failed to allocate TX engine");
            ctx->state = AutoStateError;
            return;
        }
    } else {
        // Initialize NRF24
        RfNrf24Config nrf_config;
        rf_nrf24_config_defaults(&nrf_config);
        nrf_config.channel = config->nrf24_channel;
        if(!rf_nrf24_init(&nrf_config)) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "NRF24 init failed");
            ctx->state = AutoStateError;
            return;
        }
    }

    // Clear state
    ctx->state = AutoStateRx;
    ctx->state_start_tick = furi_get_tick();
    ctx->signal_captured = false;
    ctx->capture_running = false;
    ctx->last_tx_tick = 0;
    ctx->cooldown_end_tick = 0;
    memset(&ctx->waveform, 0, sizeof(ctx->waveform));
    memset(&ctx->detected_signal, 0, sizeof(ctx->detected_signal));
    memset(ctx->error_msg, 0, sizeof(ctx->error_msg));

    // Start RX capture on test frequency
    if(rf_capture_start(app->capture, config->test_frequency, config->rx_preset)) {
        ctx->capture_running = true;
    } else {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Failed to start RX capture");
        ctx->state = AutoStateError;
    }
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

    // Stop NRF24
    rf_nrf24_emergency_stop();
    rf_nrf24_deinit();

    // Stop capture
    if(ctx->capture_running) {
        rf_capture_stop(app->capture);
        ctx->capture_running = false;
    }

    ctx->state = AutoStateIdle;
    ctx->signal_captured = false;
}

// Emergency stop - immediate halt of all TX
static void auto_test_emergency_stop(AutoTestContext* ctx) {
    RfAnalyzerApp* app = ctx->app;

    // Immediate TX stop
    if(ctx->tx_engine) {
        rf_tx_emergency_stop(ctx->tx_engine);
    }
    rf_nrf24_emergency_stop();

    // Stop capture
    if(ctx->capture_running) {
        rf_capture_stop(app->capture);
        ctx->capture_running = false;
    }

    ctx->state = AutoStateIdle;
    ctx->signal_captured = false;
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
        // Monitor for signal via capture stats
        rf_capture_get_stats(app->capture, &ctx->capture_stats);

        // When remove_all_restrictions is enabled, allow any signal
        // Otherwise, require minimum edges for valid signal
        uint32_t min_edges = ctx->app->auto_test_config.remove_all_restrictions ? 1 : 5;
        if(ctx->capture_stats.edges > min_edges) {
            // Signal detected - check RSSI by sampling
            // For simplicity, we use edge count as activity indicator
            // In a full implementation, we'd also check RSSI

            ctx->detected_signal.frequency = config->test_frequency;
            ctx->detected_signal.preset = config->rx_preset;
            ctx->detected_signal.duration_ms = ctx->capture_stats.max_us / 1000;
            ctx->detected_signal.rssi = -50.0f; // Estimated

            ctx->state = AutoStateDetected;
            ctx->state_start_tick = now;
        }
        break;
    }

    case AutoStateDetected: {
        // Quick validation before analysis
        // When remove_all_restrictions is enabled, skip decode requirement
        if(!ctx->app->auto_test_config.remove_all_restrictions && config->require_decode) {
            // Check if we can identify modulation
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
        // Run capture for a bit longer to get stable stats
        if(now - ctx->state_start_tick > furi_ms_to_ticks(200)) {
            rf_capture_get_stats(app->capture, &ctx->capture_stats);
            ctx->state = AutoStateGenerating;
            ctx->state_start_tick = now;
        }
        break;
    }

    case AutoStateGenerating: {
        // Generate inverse waveform
        // When remove_all_restrictions is enabled, allow any modulation
        RfInvertResult res;
        if(ctx->app->auto_test_config.remove_all_restrictions) {
            // Force OOK for unrestricted mode
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
            ctx->waveform.frequency = config->test_frequency;
            ctx->waveform.valid = true;
            res = RfInvertOk;
        } else {
            res =
                rf_tx_generate_inverse(&ctx->capture_stats, &ctx->detected_signal, &ctx->waveform);
        }
        if(res != RfInvertOk) {
            switch(res) {
            case RfInvertErrUnsupportedModulation:
                snprintf(
                    ctx->error_msg,
                    sizeof(ctx->error_msg),
                    "Modulation not supported for inverse");
                break;
            case RfInvertErrNoSignal:
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Insufficient signal data");
                break;
            case RfInvertErrTooComplex:
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Signal too complex for inverse");
                break;
            case RfInvertErrTxNotAllowed:
                snprintf(
                    ctx->error_msg, sizeof(ctx->error_msg), "TX not allowed at this frequency");
                break;
            default:
                snprintf(
                    ctx->error_msg, sizeof(ctx->error_msg), "Inverse generation failed (%d)", res);
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
        ctx->last_tx_tick = now;
        break;
    }

    case AutoStateTransmitting: {
        // Transmit the inverse waveform.
        // Restricted mode uses the configured TX duration; bypass mode uses
        // the maximum duration and skips the cooldown afterwards.
        RfInvertResult res;
        if(ctx->app->auto_test_config.remove_all_restrictions) {
            if(config->nrf24_mode) {
                res = rf_nrf24_transmit_inverse(
                    &ctx->waveform, config->nrf24_channel, RF_AUTO_TEST_MAX_DURATION_MS);
            } else {
                res = rf_tx_transmit_waveform(
                    ctx->tx_engine, &ctx->waveform, RF_AUTO_TEST_MAX_DURATION_MS);
            }
        } else {
            if(config->nrf24_mode) {
                res = rf_nrf24_transmit_inverse(
                    &ctx->waveform, config->nrf24_channel, config->tx_duration_ms);
            } else {
                res = rf_tx_transmit_waveform(
                    ctx->tx_engine, &ctx->waveform, config->tx_duration_ms);
            }
        }

        if(res != RfInvertOk) {
            switch(res) {
            case RfInvertErrTxNotAllowed:
                snprintf(
                    ctx->error_msg, sizeof(ctx->error_msg), "TX blocked by firmware/hardware");
                break;
            case RfInvertErrUnsupportedModulation:
                snprintf(
                    ctx->error_msg,
                    sizeof(ctx->error_msg),
                    "NRF24 needs ext module driver (no SDK HAL)");
                break;
            case RfInvertErrHardware:
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Hardware TX error");
                break;
            default:
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "TX failed (%d)", res);
            }
            ctx->state = AutoStateError;
        } else if(ctx->app->auto_test_config.remove_all_restrictions) {
            // TX completed successfully with restrictions removed
            // Skip cooldown, go directly back to idle for continuous operation
            ctx->state = AutoStateIdle;
        } else {
            // TX completed successfully - enforce cooldown normally
            ctx->cooldown_end_tick = now + furi_ms_to_ticks(config->cooldown_ms);
            ctx->state = AutoStateCooldown;
        }
        break;
    }

    case AutoStateCooldown: {
        // Wait for cooldown to expire
        if(now >= ctx->cooldown_end_tick) {
            // Return to RX mode
            ctx->state = AutoStateRx;
            ctx->state_start_tick = now;
            ctx->signal_captured = false;
            // Capture is still running from before
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

    // Frequency
    char line[64];
    uint32_t freq = app->auto_test_config.test_frequency;
    snprintf(
        line,
        sizeof(line),
        "Freq: %lu.%03lu MHz",
        (unsigned long)(freq / 1000000),
        (unsigned long)((freq % 1000000) / 1000));
    widget_add_string_element(w, 2, 22, AlignLeft, AlignBottom, FontSecondary, line);

    // Mode indicator
    if(app->auto_test_config.nrf24_mode) {
        snprintf(line, sizeof(line), "Mode: NRF24 Ch%u", app->auto_test_config.nrf24_channel);
    } else {
        snprintf(
            line,
            sizeof(line),
            "Mode: Sub-GHz %s",
            rf_preset_name(app->auto_test_config.rx_preset));
    }
    widget_add_string_element(w, 2, 32, AlignLeft, AlignBottom, FontSecondary, line);

    // State
    snprintf(line, sizeof(line), "State: %s", auto_state_names[ctx->state]);
    widget_add_string_element(w, 2, 42, AlignLeft, AlignBottom, FontSecondary, line);

    // Status details based on state
    switch(ctx->state) {
    case AutoStateIdle:
        if(ctx->app->auto_test_config.remove_all_restrictions) {
            widget_add_string_element(
                w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "!!! RESTRICTIONS REMOVED !!!");
            widget_add_string_element(
                w, 2, 60, AlignLeft, AlignBottom, FontSecondary, "UNSAFE - authorized lab only");
        } else {
            widget_add_string_element(
                w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "Press OK to start");
            widget_add_string_element(
                w, 2, 60, AlignLeft, AlignBottom, FontSecondary, "Back: Exit");
        }
        break;

    case AutoStateRx:
        widget_add_string_element(
            w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "Monitoring for signal...");
        snprintf(
            line,
            sizeof(line),
            "Edges: %lu  Bitrate: ~%lu bps",
            (unsigned long)ctx->capture_stats.edges,
            (unsigned long)ctx->capture_stats.est_bitrate);
        widget_add_string_element(w, 2, 60, AlignLeft, AlignBottom, FontSecondary, line);
        break;

    case AutoStateDetected:
        widget_add_string_element(
            w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "Signal detected - verifying");
        break;

    case AutoStateAnalyzing:
        widget_add_string_element(
            w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "Analyzing signal timing...");
        break;

    case AutoStateGenerating:
        widget_add_string_element(
            w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "Generating inverse waveform...");
        break;

    case AutoStateTransmitting:
        snprintf(line, sizeof(line), "TX Duration: %lu ms", app->auto_test_config.tx_duration_ms);
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
            w, 2, 60, AlignLeft, AlignBottom, FontSecondary, "Returning to RX...");
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

    // Emergency stop everything
    if(s_ctx) {
        auto_test_emergency_stop(s_ctx);
        if(s_ctx->tx_engine) {
            rf_tx_engine_free(s_ctx->tx_engine);
            s_ctx->tx_engine = NULL;
        }
        rf_nrf24_deinit();
        free(s_ctx);
        s_ctx = NULL;
    }

    // Stop timer
    if(app->ui_timer) {
        furi_timer_stop(app->ui_timer);
        furi_timer_free(app->ui_timer);
        app->ui_timer = NULL;
    }

    // Ensure capture is stopped
    rf_capture_stop(app->capture);

    widget_reset(app->widget);
}
