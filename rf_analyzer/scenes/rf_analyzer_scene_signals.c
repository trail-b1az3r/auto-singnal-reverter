#include "../rf_analyzer_i.h"
#include "rf_analyzer_scene.h"

/*
 * Detected-signals list scene.
 *
 * Lists every candidate signal captured this session. Selecting one opens the
 * Analyze scene parked on that frequency. A trailing "Clear list" action frees
 * the session list. Nothing here transmits.
 */

#define SIGNALS_CLEAR_ID 0xFF

static void rf_scene_signals_callback(void* context, uint32_t index) {
    RfAnalyzerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void rf_scene_signals_build(RfAnalyzerApp* app) {
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "Detected Signals");

    if(app->signal_count == 0) {
        submenu_add_item(
            menu, "(none yet - run Scan)", SIGNALS_CLEAR_ID, rf_scene_signals_callback, app);
        return;
    }

    char label[48];
    for(uint8_t i = 0; i < app->signal_count; i++) {
        const RfSignal* s = &app->signals[i];
        snprintf(
            label,
            sizeof(label),
            "%lu.%03lu %ddBm %s",
            (unsigned long)(s->frequency / 1000000),
            (unsigned long)((s->frequency % 1000000) / 1000),
            (int)s->rssi,
            s->decoded ? s->protocol : "?");
        submenu_add_item(menu, label, i, rf_scene_signals_callback, app);
    }
    submenu_add_item(menu, "Clear list", SIGNALS_CLEAR_ID, rf_scene_signals_callback, app);
}

void rf_analyzer_scene_signals_on_enter(void* context) {
    RfAnalyzerApp* app = context;
    rf_scene_signals_build(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RfViewSubmenu);
}

bool rf_analyzer_scene_signals_on_event(void* context, SceneManagerEvent event) {
    RfAnalyzerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == SIGNALS_CLEAR_ID) {
        rf_app_clear_signals(app);
        rf_scene_signals_build(app); // rebuild in place
        return true;
    }

    if(event.event < app->signal_count) {
        app->selected_signal = (uint8_t)event.event;
        app->analyze_freq = app->signals[app->selected_signal].frequency;
        scene_manager_next_scene(app->scene_manager, RfSceneAnalyze);
        return true;
    }
    return false;
}

void rf_analyzer_scene_signals_on_exit(void* context) {
    RfAnalyzerApp* app = context;
    submenu_reset(app->submenu);
}
