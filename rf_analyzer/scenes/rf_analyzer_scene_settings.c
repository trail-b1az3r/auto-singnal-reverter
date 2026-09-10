#include "../rf_analyzer_i.h"
#include "rf_analyzer_scene.h"

/*
 * Settings scene.
 *
 * Edits the scan configuration: band (which sets the sweep range), step size,
 * per-step dwell, RSSI trigger threshold and modulation preset. All values feed
 * rf_scan_config_validate() before a scan actually starts.
 */

static const uint32_t step_values[] = {25000, 50000, 100000, 250000, 500000};
static const char* const step_labels[] = {"25k", "50k", "100k", "250k", "500k"};
static const uint32_t dwell_values[] = {5, 10, 20, 50, 100};
static const char* const dwell_labels[] = {"5ms", "10ms", "20ms", "50ms", "100ms"};
static const float rssi_values[] = {-90.0f, -80.0f, -70.0f, -60.0f, -50.0f};
static const char* const rssi_labels[] = {"-90", "-80", "-70", "-60", "-50"};

static uint8_t index_of_u32(const uint32_t* arr, uint8_t n, uint32_t v, uint8_t dflt) {
    for(uint8_t i = 0; i < n; i++)
        if(arr[i] == v) return i;
    return dflt;
}
static uint8_t index_of_f(const float* arr, uint8_t n, float v, uint8_t dflt) {
    for(uint8_t i = 0; i < n; i++)
        if(arr[i] == v) return i;
    return dflt;
}

static void band_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, rf_band_name((RfBand)idx));
    rf_band_bounds((RfBand)idx, &app->config.freq_start, &app->config.freq_end);
}
static void step_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, step_labels[idx]);
    app->config.freq_step = step_values[idx];
}
static void dwell_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, dwell_labels[idx]);
    app->config.dwell_ms = dwell_values[idx];
}
static void rssi_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, rssi_labels[idx]);
    app->config.rssi_trigger = rssi_values[idx];
}
static void preset_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, rf_preset_name((RfPreset)idx));
    app->config.preset = (RfPreset)idx;
}

// Map the current freq range back to a band index for display.
static uint8_t current_band_index(RfAnalyzerApp* app) {
    for(uint8_t b = 0; b < RfBandCount; b++) {
        uint32_t s, e;
        rf_band_bounds((RfBand)b, &s, &e);
        if(app->config.freq_start == s && app->config.freq_end == e) return b;
    }
    return RfBand433;
}

void rf_analyzer_scene_settings_on_enter(void* context) {
    RfAnalyzerApp* app = context;
    VariableItemList* list = app->var_list;
    variable_item_list_reset(list);
    VariableItem* item;

    item = variable_item_list_add(list, "Band", RfBandCount, band_changed, app);
    uint8_t bidx = current_band_index(app);
    variable_item_set_current_value_index(item, bidx);
    variable_item_set_current_value_text(item, rf_band_name((RfBand)bidx));

    item = variable_item_list_add(list, "Step", COUNT_OF(step_values), step_changed, app);
    uint8_t sidx = index_of_u32(step_values, COUNT_OF(step_values), app->config.freq_step, 2);
    variable_item_set_current_value_index(item, sidx);
    variable_item_set_current_value_text(item, step_labels[sidx]);

    item = variable_item_list_add(list, "Dwell", COUNT_OF(dwell_values), dwell_changed, app);
    uint8_t didx = index_of_u32(dwell_values, COUNT_OF(dwell_values), app->config.dwell_ms, 1);
    variable_item_set_current_value_index(item, didx);
    variable_item_set_current_value_text(item, dwell_labels[didx]);

    item = variable_item_list_add(list, "RSSI trig", COUNT_OF(rssi_values), rssi_changed, app);
    uint8_t ridx = index_of_f(rssi_values, COUNT_OF(rssi_values), app->config.rssi_trigger, 2);
    variable_item_set_current_value_index(item, ridx);
    variable_item_set_current_value_text(item, rssi_labels[ridx]);

    item = variable_item_list_add(list, "Modulation", RfPresetCount, preset_changed, app);
    variable_item_set_current_value_index(item, app->config.preset);
    variable_item_set_current_value_text(item, rf_preset_name(app->config.preset));

    view_dispatcher_switch_to_view(app->view_dispatcher, RfViewVarList);
}

bool rf_analyzer_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void rf_analyzer_scene_settings_on_exit(void* context) {
    RfAnalyzerApp* app = context;
    variable_item_list_reset(app->var_list);
}
