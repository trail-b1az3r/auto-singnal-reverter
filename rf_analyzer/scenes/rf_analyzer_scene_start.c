#include "../rf_analyzer_i.h"
#include "rf_analyzer_scene.h"

typedef enum {
    StartItemScan,
    StartItemSignals,
    StartItemAnalyze,
    StartItemFreqList,
    StartItemAutoTest,
    StartItemSettings,
    StartItemAbout,
} StartItem;

static void rf_scene_start_callback(void* context, uint32_t index) {
    RfAnalyzerApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void rf_analyzer_scene_start_on_enter(void* context) {
    RfAnalyzerApp* app = context;
    Submenu* menu = app->submenu;
    submenu_reset(menu);
    submenu_set_header(menu, "RF Analyzer (RX only)");
    submenu_add_item(menu, "Scan", StartItemScan, rf_scene_start_callback, app);
    submenu_add_item(menu, "Detected Signals", StartItemSignals, rf_scene_start_callback, app);
    submenu_add_item(menu, "Analyze", StartItemAnalyze, rf_scene_start_callback, app);
    submenu_add_item(menu, "Frequency List", StartItemFreqList, rf_scene_start_callback, app);
    submenu_add_item(menu, "Auto Inverse Test", StartItemAutoTest, rf_scene_start_callback, app);
    submenu_add_item(menu, "Settings", StartItemSettings, rf_scene_start_callback, app);
    submenu_add_item(menu, "About", StartItemAbout, rf_scene_start_callback, app);
    submenu_set_selected_item(
        menu, scene_manager_get_scene_state(app->scene_manager, RfSceneStart));
    view_dispatcher_switch_to_view(app->view_dispatcher, RfViewSubmenu);
}

bool rf_analyzer_scene_start_on_event(void* context, SceneManagerEvent event) {
    RfAnalyzerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    scene_manager_set_scene_state(app->scene_manager, RfSceneStart, event.event);
    switch(event.event) {
    case StartItemScan:
        scene_manager_next_scene(app->scene_manager, RfSceneScan);
        return true;
    case StartItemSignals:
        scene_manager_next_scene(app->scene_manager, RfSceneSignals);
        return true;
    case StartItemAnalyze:
        // Analyze the highlighted signal if any, else the configured start freq.
        app->analyze_freq = app->signal_count ? app->signals[app->selected_signal].frequency :
                                                app->config.freq_start;
        scene_manager_next_scene(app->scene_manager, RfSceneAnalyze);
        return true;
    case StartItemFreqList:
        scene_manager_next_scene(app->scene_manager, RfSceneFreqList);
        return true;
    case StartItemAutoTest:
        scene_manager_next_scene(app->scene_manager, RfSceneAutoTest);
        return true;
    case StartItemSettings:
        scene_manager_next_scene(app->scene_manager, RfSceneSettings);
        return true;
    case StartItemAbout:
        scene_manager_next_scene(app->scene_manager, RfSceneAbout);
        return true;
    default:
        return false;
    }
}

void rf_analyzer_scene_start_on_exit(void* context) {
    RfAnalyzerApp* app = context;
    submenu_reset(app->submenu);
}
