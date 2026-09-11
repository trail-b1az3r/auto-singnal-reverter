#include "../rf_analyzer_i.h"
#include "rf_analyzer_scene.h"

/*
 * Analyze scene.
 *
 * Parks the receiver on app->analyze_freq and runs timing analysis on the raw
 * edge stream. The widget refreshes from a timer and shows the modulation
 * (from the active preset), live edge activity, measured pulse timing and an
 * estimated bitrate. Full protocol decoding is not attempted (the firmware
 * decoder registry is not in the public SDK), so the protocol is reported
 * honestly as "not identified".
 *
 * This scene receives and measures only. It never re-emits or "responds to"
 * the captured signal.
 */

#define ANALYZE_UI_REFRESH_MS 250

static void rf_scene_analyze_build(RfAnalyzerApp* app) {
    Widget* w = app->widget;
    widget_reset(w);

    char line[52];
    uint32_t f = app->analyze_freq;

    widget_add_string_element(w, 2, 10, AlignLeft, AlignBottom, FontPrimary, "Analyze  [RX]");

    snprintf(
        line,
        sizeof(line),
        "%lu.%03lu MHz  %s",
        (unsigned long)(f / 1000000),
        (unsigned long)((f % 1000000) / 1000),
        rf_capture_modulation_hint(app->capture));
    widget_add_string_element(w, 2, 22, AlignLeft, AlignBottom, FontSecondary, line);

    RfCaptureStats st;
    rf_capture_get_stats(app->capture, &st);

    if(st.edges == 0) {
        widget_add_string_element(
            w, 2, 36, AlignLeft, AlignBottom, FontSecondary, "Listening... no activity");
    } else {
        snprintf(line, sizeof(line), "Edges: %lu", (unsigned long)st.edges);
        widget_add_string_element(w, 2, 34, AlignLeft, AlignBottom, FontSecondary, line);

        snprintf(
            line,
            sizeof(line),
            "Pulse: %lu/%lu/%lu us",
            (unsigned long)st.min_us,
            (unsigned long)st.avg_us,
            (unsigned long)st.max_us);
        widget_add_string_element(w, 2, 44, AlignLeft, AlignBottom, FontSecondary, line);

        if(st.est_bitrate) {
            snprintf(line, sizeof(line), "~%lu bps", (unsigned long)st.est_bitrate);
            widget_add_string_element(w, 2, 54, AlignLeft, AlignBottom, FontSecondary, line);
        }
    }

    // Always state the decode status plainly.
    widget_add_string_element(w, 126, 34, AlignRight, AlignBottom, FontSecondary, "proto:");
    widget_add_string_element(w, 126, 44, AlignRight, AlignBottom, FontSecondary, "not ID'd");
    widget_add_string_element(w, 126, 62, AlignRight, AlignBottom, FontSecondary, "Back:stop");
}

static void rf_scene_analyze_timer_cb(void* context) {
    RfAnalyzerApp* app = context;
    // Rebuild each tick so edge activity and timing animate while listening.
    // Stats are plain 32-bit scalars updated in RX interrupt context; reading
    // them here needs no lock.
    rf_scene_analyze_build(app);
}

void rf_analyzer_scene_analyze_on_enter(void* context) {
    RfAnalyzerApp* app = context;

    rf_scene_analyze_build(app);

    if(rf_capture_start(app->capture, app->analyze_freq, app->config.preset)) {
        app->ui_timer = furi_timer_alloc(rf_scene_analyze_timer_cb, FuriTimerTypePeriodic, app);
        furi_timer_start(app->ui_timer, furi_ms_to_ticks(ANALYZE_UI_REFRESH_MS));
    } else {
        widget_reset(app->widget);
        widget_add_string_element(
            app->widget, 64, 32, AlignCenter, AlignCenter, FontPrimary, "Invalid frequency");
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
