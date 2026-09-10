#include "../rf_analyzer_i.h"
#include "rf_analyzer_scene.h"

/*
 * About / safety scene. Static informational text. States plainly that this
 * build is receive-only and is intended for authorized RF research.
 */

void rf_analyzer_scene_about_on_enter(void* context) {
    RfAnalyzerApp* app = context;
    Widget* w = app->widget;
    widget_reset(w);

    widget_add_string_element(
        w, 64, 10, AlignCenter, AlignBottom, FontPrimary, "RF Analyzer (RX)");

    const char* body =
        "Receive-only Sub-GHz research tool.\n"
        "\n"
        "Scans, measures RSSI, captures and decodes signals using the built-in "
        "CC1101 radio. It does NOT transmit, replay or jam - there is no TX code "
        "path in this build.\n"
        "\n"
        "Use only on frequencies and equipment you own or are explicitly "
        "authorized to test, and follow the RF regulations for your region.\n"
        "\n"
        "Target: official Flipper Zero firmware SDK (lib/subghz subghz_devices)."
        " Built with ufbt.";
    widget_add_text_scroll_element(w, 0, 16, 128, 48, body);

    view_dispatcher_switch_to_view(app->view_dispatcher, RfViewWidget);
}

bool rf_analyzer_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void rf_analyzer_scene_about_on_exit(void* context) {
    RfAnalyzerApp* app = context;
    widget_reset(app->widget);
}
