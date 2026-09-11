#include "../rf_analyzer_i.h"
#include "rf_analyzer_scene.h"

/*
 * Settings scene.
 *
 * Edits the scan configuration and Auto Inverse Test configuration.
 * Auto Test settings include: enable, test frequency, TX duration, cooldown,
 * RSSI threshold, modulation, decode requirement, NRF24 mode, and cooldown override.
 */

static const uint32_t step_values[] = {25000, 50000, 100000, 250000, 500000};
static const char* const step_labels[] = {"25k", "50k", "100k", "250k", "500k"};
static const uint32_t dwell_values[] = {5, 10, 20, 50, 100};
static const char* const dwell_labels[] = {"5ms", "10ms", "20ms", "50ms", "100ms"};
static const float rssi_values[] = {-90.0f, -80.0f, -70.0f, -60.0f, -50.0f};
static const char* const rssi_labels[] = {"-90", "-80", "-70", "-60", "-50"};

// Auto Test configuration values
static const uint32_t tx_duration_values[] = {1, 10, 50, 100, 200, 500, 1000, 2000, 5000, 10000};
static const char* const tx_duration_labels[] = {"1ms", "10ms", "50ms", "100ms", "200ms", "500ms", "1s", "2s", "5s", "10s"};

static const uint32_t cooldown_values[] = {0, 100, 250, 500, 1000, 2000, 5000, 10000, 30000, 60000};
static const char* const cooldown_labels[] = {"OFF", "100ms", "250ms", "500ms", "1s", "2s", "5s", "10s", "30s", "60s"};

static const uint32_t test_freq_values[] = {
    315000000, 433920000, 868300000, 915000000
};
static const char* const test_freq_labels[] = {
    "315.000 MHz", "433.920 MHz", "868.300 MHz", "915.000 MHz"
};

static const uint8_t nrf24_channels[] = {0, 1, 2, 10, 20, 40, 60, 80, 100, 125};
static const char* const nrf24_channel_labels[] = {"Ch 0", "Ch 1", "Ch 2", "Ch 10", "Ch 20", "Ch 40", "Ch 60", "Ch 80", "Ch 100", "Ch 125"};

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

// Auto Test callbacks
static void auto_test_enabled_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, idx ? "ON" : "OFF");
    app->auto_test_config.enabled = idx;
}

static void auto_test_freq_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, test_freq_labels[idx]);
    app->auto_test_config.test_frequency = test_freq_values[idx];
}

static void auto_test_tx_duration_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, tx_duration_labels[idx]);
    app->auto_test_config.tx_duration_ms = tx_duration_values[idx];
}

static void auto_test_cooldown_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, cooldown_labels[idx]);
    app->auto_test_config.cooldown_ms = cooldown_values[idx];
}

static void auto_test_rssi_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, rssi_labels[idx]);
    app->auto_test_config.rssi_threshold = rssi_values[idx];
}

static void auto_test_rx_preset_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, rf_preset_name((RfPreset)idx));
    app->auto_test_config.rx_preset = (RfPreset)idx;
}

static void auto_test_require_decode_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, idx ? "YES" : "NO");
    app->auto_test_config.require_decode = idx;
}

static void auto_test_nrf24_mode_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, idx ? "ON" : "OFF");
    app->auto_test_config.nrf24_mode = idx;
}

static void auto_test_nrf24_channel_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, nrf24_channel_labels[idx]);
    app->auto_test_config.nrf24_channel = nrf24_channels[idx];
}

static void auto_test_remove_cooldown_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, idx ? "YES (Unsafe)" : "NO");
    app->auto_test_config.remove_cooldown_limit = idx;
}

static void auto_test_remove_all_restrictions_changed(VariableItem* item) {
    RfAnalyzerApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, idx ? "YES (Unsafe)" : "NO");
    app->auto_test_config.remove_all_restrictions = idx;
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

// Find index of test frequency in our predefined list
static uint8_t current_test_freq_index(RfAnalyzerApp* app) {
    for(uint8_t i = 0; i < COUNT_OF(test_freq_values); i++) {
        if(test_freq_values[i] == app->auto_test_config.test_frequency) return i;
    }
    return 1; // Default to 433.92 MHz
}

static uint8_t current_tx_duration_index(RfAnalyzerApp* app) {
    for(uint8_t i = 0; i < COUNT_OF(tx_duration_values); i++) {
        if(tx_duration_values[i] == app->auto_test_config.tx_duration_ms) return i;
    }
    return 3; // Default 100ms
}

static uint8_t current_cooldown_index(RfAnalyzerApp* app) {
    for(uint8_t i = 0; i < COUNT_OF(cooldown_values); i++) {
        if(cooldown_values[i] == app->auto_test_config.cooldown_ms) return i;
    }
    return 4; // Default 1000ms
}

static uint8_t current_rssi_index(RfAnalyzerApp* app) {
    for(uint8_t i = 0; i < COUNT_OF(rssi_values); i++) {
        if(rssi_values[i] == app->auto_test_config.rssi_threshold) return i;
    }
    return 2; // Default -70
}

static uint8_t current_nrf24_channel_index(RfAnalyzerApp* app) {
    for(uint8_t i = 0; i < COUNT_OF(nrf24_channels); i++) {
        if(nrf24_channels[i] == app->auto_test_config.nrf24_channel) return i;
    }
    return 2; // Default Ch 2
}

void rf_analyzer_scene_settings_on_enter(void* context) {
    RfAnalyzerApp* app = context;
    VariableItemList* list = app->var_list;
    variable_item_list_reset(list);
    VariableItem* item;

    // --- Scan Settings ---
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

    // --- Auto Inverse Test Settings ---
    static void dummy_cb(VariableItem* item) { UNUSED(item); }
    item = variable_item_list_add(list, "--- Auto Test ---", 1, dummy_cb, app);
    variable_item_set_current_value_text(item, "");

    item = variable_item_list_add(list, "Auto Inverse", 2, auto_test_enabled_changed, app);
    variable_item_set_current_value_index(item, app->auto_test_config.enabled ? 1 : 0);
    variable_item_set_current_value_text(item, app->auto_test_config.enabled ? "ON" : "OFF");

    item = variable_item_list_add(list, "Test Frequency", COUNT_OF(test_freq_values), auto_test_freq_changed, app);
    variable_item_set_current_value_index(item, current_test_freq_index(app));
    variable_item_set_current_value_text(item, test_freq_labels[current_test_freq_index(app)]);

    item = variable_item_list_add(list, "TX Duration", COUNT_OF(tx_duration_values), auto_test_tx_duration_changed, app);
    variable_item_set_current_value_index(item, current_tx_duration_index(app));
    variable_item_set_current_value_text(item, tx_duration_labels[current_tx_duration_index(app)]);

    item = variable_item_list_add(list, "Cooldown", COUNT_OF(cooldown_values), auto_test_cooldown_changed, app);
    variable_item_set_current_value_index(item, current_cooldown_index(app));
    variable_item_set_current_value_text(item, cooldown_labels[current_cooldown_index(app)]);

    item = variable_item_list_add(list, "RSSI Threshold", COUNT_OF(rssi_values), auto_test_rssi_changed, app);
    variable_item_set_current_value_index(item, current_rssi_index(app));
    variable_item_set_current_value_text(item, rssi_labels[current_rssi_index(app)]);

    item = variable_item_list_add(list, "RX Modulation", RfPresetCount, auto_test_rx_preset_changed, app);
    variable_item_set_current_value_index(item, app->auto_test_config.rx_preset);
    variable_item_set_current_value_text(item, rf_preset_name(app->auto_test_config.rx_preset));

    item = variable_item_list_add(list, "Require Decode", 2, auto_test_require_decode_changed, app);
    variable_item_set_current_value_index(item, app->auto_test_config.require_decode ? 1 : 0);
    variable_item_set_current_value_text(item, app->auto_test_config.require_decode ? "YES" : "NO");

    item = variable_item_list_add(list, "NRF24 Mode", 2, auto_test_nrf24_mode_changed, app);
    variable_item_set_current_value_index(item, app->auto_test_config.nrf24_mode ? 1 : 0);
    variable_item_set_current_value_text(item, app->auto_test_config.nrf24_mode ? "ON" : "OFF");

    item = variable_item_list_add(list, "NRF24 Channel", COUNT_OF(nrf24_channels), auto_test_nrf24_channel_changed, app);
    variable_item_set_current_value_index(item, current_nrf24_channel_index(app));
    variable_item_set_current_value_text(item, nrf24_channel_labels[current_nrf24_channel_index(app)]);

    item = variable_item_list_add(list, "Remove Cooldown Limit", 2, auto_test_remove_cooldown_changed, app);
    variable_item_set_current_value_index(item, app->auto_test_config.remove_cooldown_limit ? 1 : 0);
    variable_item_set_current_value_text(item, app->auto_test_config.remove_cooldown_limit ? "YES (Unsafe)" : "NO");

    item = variable_item_list_add(list, "Remove All Restrictions", 2, auto_test_remove_all_restrictions_changed, app);
    variable_item_set_current_value_index(item, app->auto_test_config.remove_all_restrictions ? 1 : 0);
    variable_item_set_current_value_text(item, app->auto_test_config.remove_all_restrictions ? "YES (Unsafe)" : "NO");

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
