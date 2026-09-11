#include "rf_analyzer_i.h"
#include "scenes/rf_analyzer_scene.h"
#include "helpers/rf_analyzer_tx.h"

/*
 * RF Analyzer — application entry point, wiring and session state.
 *
 * Receive-first by construction: the always-on engines are the scanner (RSSI
 * sweep) and the capture/decoder. A TX engine exists solely for the optional
 * Auto Inverse Test mode, which is disabled by default and gated by explicit
 * user enable, a single configured test frequency, max TX duration, cooldown,
 * decode requirement, emergency stop and firmware validity checks.
 */

#define TAG "RfAnalyzer"

/* ---------- session-list helpers ---------- */

// Treat detections within this tolerance as the same signal (step granularity).
#define RF_FREQ_MATCH_HZ 5000

void rf_app_add_signal(RfAnalyzerApp* app, const RfSignal* signal) {
    // Merge into an existing nearby entry, keeping the strongest RSSI seen.
    for(uint8_t i = 0; i < app->signal_count; i++) {
        uint32_t a = app->signals[i].frequency;
        uint32_t b = signal->frequency;
        uint32_t diff = (a > b) ? (a - b) : (b - a);
        if(diff <= RF_FREQ_MATCH_HZ) {
            if(signal->rssi > app->signals[i].rssi) app->signals[i].rssi = signal->rssi;
            app->signals[i].duration_ms += signal->duration_ms;
            return;
        }
    }
    // New entry. Evict the oldest (index 0) when the list is full.
    if(app->signal_count >= RF_ANALYZER_MAX_SIGNALS) {
        memmove(
            &app->signals[0], &app->signals[1], sizeof(RfSignal) * (RF_ANALYZER_MAX_SIGNALS - 1));
        app->signal_count = RF_ANALYZER_MAX_SIGNALS - 1;
    }
    app->signals[app->signal_count++] = *signal;
}

void rf_app_clear_signals(RfAnalyzerApp* app) {
    app->signal_count = 0;
    app->selected_signal = 0;
}

bool rf_app_add_freq(RfAnalyzerApp* app, uint32_t freq) {
    for(uint8_t i = 0; i < app->freq_list_count; i++) {
        if(app->freq_list[i] == freq) return true; // already present
    }
    if(app->freq_list_count >= RF_ANALYZER_MAX_FREQ_LIST) return false;
    app->freq_list[app->freq_list_count++] = freq;
    return true;
}

void rf_app_remove_freq(RfAnalyzerApp* app, uint8_t index) {
    if(index >= app->freq_list_count) return;
    memmove(
        &app->freq_list[index],
        &app->freq_list[index + 1],
        sizeof(uint32_t) * (app->freq_list_count - index - 1));
    app->freq_list_count--;
}

/* ---------- RF engine callbacks ---------- */

// Runs on the scanner thread. The session list is only browsed once scanning
// has stopped, so appending here does not race the GUI reads.
static void rf_analyzer_on_signal(const RfSignal* signal, void* context) {
    RfAnalyzerApp* app = context;
    rf_app_add_signal(app, signal);
    notification_message(app->notifications, &sequence_blink_blue_10);
}

// Re-registers the default Scan-scene callback after a scene (e.g. Auto
// Inverse Test) temporarily borrowed the scanner with its own callback.
void rf_app_scanner_restore_callback(RfAnalyzerApp* app) {
    rf_scanner_set_callback(app->scanner, rf_analyzer_on_signal, app);
}

/* ---------- view dispatcher glue ---------- */

static bool rf_analyzer_custom_event_cb(void* context, uint32_t event) {
    RfAnalyzerApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool rf_analyzer_back_event_cb(void* context) {
    RfAnalyzerApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

/* ---------- lifecycle ---------- */

static void rf_analyzer_config_defaults(RfAnalyzerApp* app) {
    rf_band_bounds(RfBand433, &app->config.freq_start, &app->config.freq_end);
    app->config.freq_step = 100000; // 100 kHz
    app->config.dwell_ms = 10; // 10 ms per step
    app->config.rssi_trigger = -70.0f; // dBm
    app->config.preset = RfPresetOok650;

    rf_auto_test_config_defaults(&app->auto_test_config);
}

static RfAnalyzerApp* rf_analyzer_app_alloc(void) {
    RfAnalyzerApp* app = malloc(sizeof(RfAnalyzerApp));
    memset(app, 0, sizeof(RfAnalyzerApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&rf_analyzer_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, rf_analyzer_custom_event_cb);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, rf_analyzer_back_event_cb);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Views
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, RfViewSubmenu, submenu_get_view(app->submenu));
    app->var_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RfViewVarList, variable_item_list_get_view(app->var_list));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, RfViewWidget, widget_get_view(app->widget));
    app->scan_view = rf_scan_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RfViewScan, rf_scan_view_get_view(app->scan_view));

    // Initialise the Sub-GHz device registry once so the engines can resolve
    // the built-in CC1101 by name.
    subghz_devices_init();

    // RF engines (receive only)
    app->scanner = rf_scanner_alloc();
    rf_scanner_set_callback(app->scanner, rf_analyzer_on_signal, app);
    app->capture = rf_capture_alloc();

    // TX engine for Auto Inverse Test (lazy init, but allocate here for cleanup)
    app->tx_engine = rf_tx_engine_alloc();

    rf_analyzer_config_defaults(app);
    return app;
}

static void rf_analyzer_app_free(RfAnalyzerApp* app) {
    // Ensure the radio is released before tearing anything down.
    rf_scanner_stop(app->scanner);
    rf_capture_stop(app->capture);
    rf_scanner_free(app->scanner);
    rf_capture_free(app->capture);

    // Free TX engine
    if(app->tx_engine) {
        rf_tx_engine_free(app->tx_engine);
        app->tx_engine = NULL;
    }

    // Ensure NRF24 is deinitialized
    rf_nrf24_deinit();

    view_dispatcher_remove_view(app->view_dispatcher, RfViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, RfViewVarList);
    view_dispatcher_remove_view(app->view_dispatcher, RfViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, RfViewScan);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_list);
    widget_free(app->widget);
    rf_scan_view_free(app->scan_view);

    subghz_devices_deinit();

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t rf_analyzer_app(void* p) {
    UNUSED(p);
    RfAnalyzerApp* app = rf_analyzer_app_alloc();

    scene_manager_next_scene(app->scene_manager, RfSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    rf_analyzer_app_free(app);
    return 0;
}
