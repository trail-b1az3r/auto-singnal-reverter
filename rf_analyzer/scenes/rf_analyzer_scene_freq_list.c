#include "../rf_analyzer_i.h"
#include "rf_analyzer_scene.h"

/*
 * Frequency-list scene.
 *
 * The temporary session list of frequencies of interest. The user can import
 * everything found during scanning, then step through entries manually;
 * selecting one opens Analyze parked on that frequency (receive/decode only).
 * There is no sequenced transmit mode — stepping only re-parks the receiver.
 */

#define FREQ_IMPORT_ID 0xF0
#define FREQ_CLEAR_ID  0xF1

static void rf_scene_freq_callback(void* context, uint32_t index) {
    RfAnalyzerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void rf_scene_freq_build(RfAnalyzerApp* app) {
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "Frequency List");

    char label[40];
    for(uint8_t i = 0; i < app->freq_list_count; i++) {
        uint32_t f = app->freq_list[i];
        snprintf(
            label,
            sizeof(label),
            "%c %lu.%03lu MHz",
            (i == app->freq_list_current) ? '>' : ' ',
            (unsigned long)(f / 1000000),
            (unsigned long)((f % 1000000) / 1000));
        submenu_add_item(menu, label, i, rf_scene_freq_callback, app);
    }
    submenu_add_item(menu, "Import detected", FREQ_IMPORT_ID, rf_scene_freq_callback, app);
    if(app->freq_list_count) {
        submenu_add_item(menu, "Clear list", FREQ_CLEAR_ID, rf_scene_freq_callback, app);
    }
}

void rf_analyzer_scene_freq_list_on_enter(void* context) {
    RfAnalyzerApp* app = context;
    rf_scene_freq_build(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RfViewSubmenu);
}

bool rf_analyzer_scene_freq_list_on_event(void* context, SceneManagerEvent event) {
    RfAnalyzerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case FREQ_IMPORT_ID:
        for(uint8_t i = 0; i < app->signal_count; i++) {
            rf_app_add_freq(app, app->signals[i].frequency);
        }
        rf_scene_freq_build(app);
        return true;
    case FREQ_CLEAR_ID:
        app->freq_list_count = 0;
        app->freq_list_current = 0;
        rf_scene_freq_build(app);
        return true;
    default:
        if(event.event < app->freq_list_count) {
            app->freq_list_current = (uint8_t)event.event; // manual cycle position
            app->analyze_freq = app->freq_list[app->freq_list_current];
            scene_manager_next_scene(app->scene_manager, RfSceneAnalyze);
            return true;
        }
        return false;
    }
}

void rf_analyzer_scene_freq_list_on_exit(void* context) {
    RfAnalyzerApp* app = context;
    submenu_reset(app->submenu);
}
