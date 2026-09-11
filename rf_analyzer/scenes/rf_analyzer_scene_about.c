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
        w, 64, 10, AlignCenter, AlignBottom, FontPrimary, "RF Analyzer v2.0");

    const char* body =
        "Sub-GHz research tool with optional Auto Inverse Test.\n"
        "\n"
        "Primary mode: receive-only scanner, RSSI measurement, signal capture "
        "and timing analysis using the built-in CC1101.\n"
        "\n"
        "Auto Inverse Test (disabled by default): when explicitly enabled, "
        "monitors a single configured frequency, detects supported signals, "
        "generates a logical waveform inverse, and transmits it for a bounded "
        "duration with enforced cooldown. NRF24 mode also available.\n"
        "\n"
        "Safeguards: disabled by default, single test frequency, max TX duration, "
        "cooldown period, decode requirement, emergency stop (Back button), "
        "prominent AUTO TX indicator, respects firmware restrictions.\n"
        "\n"
        "Use only on frequencies and equipment you own or are explicitly "
        "authorized to test, and follow RF regulations for your region.\n"
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
