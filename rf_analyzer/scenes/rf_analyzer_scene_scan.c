#include "../rf_analyzer_i.h"
#include "rf_analyzer_scene.h"

/*
 * Live scan scene.
 *
 * Starts the RSSI-sweep scanner and refreshes the custom scan view from a
 * periodic timer. OK pauses/resumes the sweep; Back stops it and returns. The
 * radio is only ever in RX or idle here.
 */

#define SCAN_UI_REFRESH_MS 100

typedef enum {
    ScanEventBack = 100,
    ScanEventToggle,
} ScanEvent;

static void rf_scene_scan_ok_cb(void* context) {
    RfAnalyzerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ScanEventToggle);
}

static void rf_scene_scan_back_cb(void* context) {
    RfAnalyzerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ScanEventBack);
}

// Timer runs on the timer service thread; view-model updates are lock-protected
// so this is safe without hopping to the GUI thread.
static void rf_scene_scan_timer_cb(void* context) {
    RfAnalyzerApp* app = context;
    bool running = rf_scanner_is_running(app->scanner);
    rf_scan_view_set_status(
        app->scan_view,
        rf_scanner_current_freq(app->scanner),
        rf_scanner_current_rssi(app->scanner),
        running,
        app->signal_count);
}

void rf_analyzer_scene_scan_on_enter(void* context) {
    RfAnalyzerApp* app = context;

    // Refuse to start on an invalid config; drop back with a status view.
    const char* err = rf_scan_config_validate(&app->config);
    if(err) {
        // Surface the reason via the widget scene instead of scanning.
        widget_reset(app->widget);
        widget_add_string_element(
            app->widget, 64, 20, AlignCenter, AlignCenter, FontPrimary, "Bad scan config");
        widget_add_string_element(
            app->widget, 64, 38, AlignCenter, AlignCenter, FontSecondary, err);
        view_dispatcher_switch_to_view(app->view_dispatcher, RfViewWidget);
        return;
    }

    rf_scan_view_set_range(app->scan_view, app->config.freq_start, app->config.freq_end);
    rf_scan_view_set_ok_callback(app->scan_view, rf_scene_scan_ok_cb, app);
    rf_scan_view_set_back_callback(app->scan_view, rf_scene_scan_back_cb, app);
    rf_scan_view_set_status(
        app->scan_view, app->config.freq_start, -110.0f, true, app->signal_count);

    rf_scanner_start(app->scanner, &app->config);

    app->ui_timer = furi_timer_alloc(rf_scene_scan_timer_cb, FuriTimerTypePeriodic, app);
    furi_timer_start(app->ui_timer, furi_ms_to_ticks(SCAN_UI_REFRESH_MS));

    view_dispatcher_switch_to_view(app->view_dispatcher, RfViewScan);
}

bool rf_analyzer_scene_scan_on_event(void* context, SceneManagerEvent event) {
    RfAnalyzerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case ScanEventToggle:
        if(rf_scanner_is_running(app->scanner)) {
            rf_scanner_stop(app->scanner);
        } else {
            rf_scanner_start(app->scanner, &app->config);
        }
        return true;
    case ScanEventBack:
        scene_manager_previous_scene(app->scene_manager);
        return true;
    default:
        return false;
    }
}

void rf_analyzer_scene_scan_on_exit(void* context) {
    RfAnalyzerApp* app = context;
    if(app->ui_timer) {
        furi_timer_stop(app->ui_timer);
        furi_timer_free(app->ui_timer);
        app->ui_timer = NULL;
    }
    rf_scanner_stop(app->scanner); // guarantees the radio is released
    widget_reset(app->widget);
}
