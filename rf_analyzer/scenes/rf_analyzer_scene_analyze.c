#include "../rf_analyzer_i.h"
#include "rf_analyzer_scene.h"

/*
 * Analyze scene.
 *
 * Parks the receiver on app->analyze_freq and runs the protocol decoders. The
 * widget refreshes from a timer: until a frame decodes it shows "Listening",
 * and when a decoder succeeds it shows the protocol name and the decoded
 * description. If nothing decodes the signal is clearly reported as Unknown.
 *
 * This scene receives and decodes only. It never re-emits or "responds to" the
 * captured signal.
 */

#define ANALYZE_UI_REFRESH_MS 250

static void rf_scene_analyze_build(RfAnalyzerApp* app) {
    Widget* w = app->widget;
    widget_reset(w);

    char line[48];
    uint32_t f = app->analyze_freq;
    snprintf(
        line,
        sizeof(line),
        "%lu.%03lu MHz  %s",
        (unsigned long)(f / 1000000),
        (unsigned long)((f % 1000000) / 1000),
        rf_preset_name(app->config.preset));
    widget_add_string_element(w, 2, 10, AlignLeft, AlignBottom, FontPrimary, "Analyze  [RX]");
    widget_add_string_element(w, 2, 24, AlignLeft, AlignBottom, FontSecondary, line);

    bool have = furi_string_size(app->last_decode_proto) > 0;
    if(have) {
        snprintf(line, sizeof(line), "Proto: %s", furi_string_get_cstr(app->last_decode_proto));
        widget_add_string_element(w, 2, 38, AlignLeft, AlignBottom, FontSecondary, line);
        // Scrollable details of the decoded frame.
        widget_add_text_scroll_element(
            w, 2, 42, 124, 20, furi_string_get_cstr(app->last_decode_text));
    } else {
        snprintf(
            line, sizeof(line), "Listening... %lu edges",
            (unsigned long)rf_capture_edge_count(app->capture));
        widget_add_string_element(w, 2, 38, AlignLeft, AlignBottom, FontSecondary, line);
        widget_add_string_element(
            w, 2, 52, AlignLeft, AlignBottom, FontSecondary, "Unknown / cannot decode");
    }
    widget_add_string_element(w, 126, 62, AlignRight, AlignBottom, FontSecondary, "Back:stop");
}

static void rf_scene_analyze_timer_cb(void* context) {
    RfAnalyzerApp* app = context;
    // Rebuild every tick so the edge-activity counter animates while listening.
    // Hold the decode mutex across the rebuild: it reads the shared decode
    // strings that the capture worker may be writing. The widget copies the
    // text internally, so the lock only needs to span the build.
    furi_mutex_acquire(app->decode_mutex, FuriWaitForever);
    app->decode_updated = false;
    rf_scene_analyze_build(app);
    furi_mutex_release(app->decode_mutex);
}

void rf_analyzer_scene_analyze_on_enter(void* context) {
    RfAnalyzerApp* app = context;

    // Reset last-decode strings for this session of the scene.
    furi_string_reset(app->last_decode_proto);
    furi_string_reset(app->last_decode_text);
    app->decode_updated = false;

    if(!furi_hal_subghz_is_frequency_valid(app->analyze_freq)) {
        widget_reset(app->widget);
        widget_add_string_element(
            app->widget, 64, 32, AlignCenter, AlignCenter, FontPrimary, "Invalid frequency");
    } else {
        rf_scene_analyze_build(app);
        rf_capture_start(app->capture, app->analyze_freq, app->config.preset);
        app->ui_timer =
            furi_timer_alloc(rf_scene_analyze_timer_cb, FuriTimerTypePeriodic, app);
        furi_timer_start(app->ui_timer, furi_ms_to_ticks(ANALYZE_UI_REFRESH_MS));
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, RfViewWidget);
}

bool rf_analyzer_scene_analyze_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false; // Back is handled by the scene manager's navigation handler.
}

void rf_analyzer_scene_analyze_on_exit(void* context) {
    RfAnalyzerApp* app = context;
    if(app->ui_timer) {
        furi_timer_stop(app->ui_timer);
        furi_timer_free(app->ui_timer);
        app->ui_timer = NULL;
    }
    rf_capture_stop(app->capture); // releases the radio
    widget_reset(app->widget);
}
