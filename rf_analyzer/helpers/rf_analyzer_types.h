#pragma once

/*
 * Shared data types for the RF Analyzer.
 *
 * The application is primarily receive-only. An optional Auto Inverse Test mode
 * can transmit a logical inverse of a captured signal for authorized lab testing.
 * All TX paths are gated by explicit user enable, configured frequency range,
 * maximum duration, cooldown, and firmware legality checks.
 */

#include <furi.h>
#include <furi_hal.h>

// Hard limits. The Flipper has a small heap, so the session list is capped and
// old entries are evicted rather than allowed to exhaust memory.
#define RF_ANALYZER_MAX_SIGNALS    32
#define RF_ANALYZER_MAX_FREQ_LIST  32
#define RF_ANALYZER_PROTO_NAME_LEN 32

// Auto Inverse Test limits
#define RF_AUTO_TEST_MAX_DURATION_MS 10000 // 10 seconds max TX
#define RF_AUTO_TEST_MAX_COOLDOWN_MS 60000 // 60 seconds max cooldown
#define RF_AUTO_TEST_MIN_DURATION_MS 1 // 1 ms minimum
#define RF_AUTO_TEST_MIN_COOLDOWN_MS 0 // 0 = no cooldown (user override)
#define RF_AUTO_TEST_MAX_PULSES      256 // Max pulse edges to capture/invert

// Sub-GHz bands the CC1101 in the Flipper Zero can tune. These are the coarse
// ranges the firmware's frequency table allows; the actual per-frequency
// legality check is delegated to subghz_devices_is_frequency_valid().
typedef enum {
    RfBand300 = 0, // 300.000 - 348.000 MHz
    RfBand433, // 387.000 - 464.000 MHz
    RfBand868, // 779.000 - 928.000 MHz
    RfBandCount,
} RfBand;

// Modulation preset used when parking on a frequency for decode. Maps onto the
// firmware's FuriHalSubGhzPreset values.
typedef enum {
    RfPresetOok650 = 0, // OOK, 650 kHz RX bandwidth (most common for remotes)
    RfPresetOok270, // OOK, 270 kHz RX bandwidth
    RfPreset2FskDev238, // 2-FSK, 2.38 kHz deviation
    RfPreset2FskDev476, // 2-FSK, 47.6 kHz deviation
    RfPresetCount,
} RfPreset;

// One observed candidate signal. Populated entirely from receive-side data.
typedef struct {
    uint32_t frequency; // Hz
    float rssi; // dBm (peak observed during dwell)
    uint32_t detected_at; // furi_get_tick() when first seen
    uint32_t duration_ms; // burst length while above threshold
    bool decoded; // true if a protocol was identified
    RfPreset preset; // modulation preset active at detection
    char protocol[RF_ANALYZER_PROTO_NAME_LEN]; // decoded protocol name or "Unknown"
    uint64_t data; // decoded payload (0 if not decoded)
    uint32_t data_bits; // payload bit count (0 if not decoded)
} RfSignal;

// User-configurable scan parameters. Range is validated against the hardware
// before a scan starts.
typedef struct {
    uint32_t freq_start; // Hz, inclusive
    uint32_t freq_end; // Hz, inclusive
    uint32_t freq_step; // Hz between sample points
    float rssi_trigger; // dBm; a sample above this counts as activity
    uint32_t dwell_ms; // time spent sampling each frequency
    RfPreset preset; // modulation preset used while scanning/decoding
} RfScanConfig;

// Human-readable helpers (implemented in rf_analyzer_scanner.c).
const char* rf_preset_name(RfPreset preset);
FuriHalSubGhzPreset rf_preset_to_hal(RfPreset preset);
const char* rf_band_name(RfBand band);
void rf_band_bounds(RfBand band, uint32_t* start, uint32_t* end);

// Snapshot of the timing analysis. All durations are in microseconds.
// Defined here (rather than in rf_analyzer_capture.h) so both the capture
// engine and the TX inverse generator can use it without a header cycle.
typedef struct {
    uint32_t edges; // level transitions seen since start
    uint32_t min_us; // shortest qualifying pulse (0 if none yet)
    uint32_t max_us; // longest qualifying pulse
    uint32_t avg_us; // mean qualifying pulse width
    uint32_t est_bitrate; // bits/sec estimated from the shortest symbol (0 if n/a)
} RfCaptureStats;

// Sub-GHz modulation types supported for TX inverse generation
typedef enum {
    RfModOOK = 0,
    RfMod2FSK,
    RfModNRF24,
    RfModCount,
} RfModulation;

// TX waveform descriptor — a sequence of (level, duration_us) edges.
// Level: 0 = low/space, 1 = high/mark. Duration in microseconds.
typedef struct {
    bool level;
    uint32_t duration_us;
} RfTxEdge;

typedef struct {
    RfTxEdge edges[RF_AUTO_TEST_MAX_PULSES];
    uint16_t edge_count;
    uint32_t total_duration_us;
    RfModulation modulation;
    uint32_t frequency; // Hz
    uint32_t bitrate; // estimated bits/sec
    bool valid; // true if waveform was successfully generated
} RfTxWaveform;

// Auto Inverse Test configuration.
//
// Discovery is range-driven: the test sweeps the main scan configuration
// (Settings → Band/Step/Dwell/RSSI trig/Modulation) and automatically
// engages ANY frequency where a signal is detected.
typedef struct {
    bool enabled; // master enable (disabled by default)
    uint32_t tx_duration_ms; // max TX on-time per trigger
    uint32_t cooldown_ms; // minimum gap between TX bursts (0 = no limit)
    bool require_decode; // only TX if protocol was decoded
    bool nrf24_mode; // use NRF24 radio instead of Sub-GHz
    uint8_t nrf24_channel; // NRF24 channel (0-125)
    bool remove_cooldown_limit; // allow 0 cooldown (full automation override)
    bool remove_all_restrictions; // when enabled: bypasses ALL checks (freq, duration, decode, limits)
} RfAutoTestConfig;

// Default configuration for Auto Inverse Test
static inline void rf_auto_test_config_defaults(RfAutoTestConfig* config) {
    config->enabled = false;
    config->tx_duration_ms = 100;
    config->cooldown_ms = 1000;
    config->require_decode = true;
    config->nrf24_mode = false;
    config->nrf24_channel = 2; // Common NRF24 default
    config->remove_cooldown_limit = false;
    config->remove_all_restrictions = false;
}

// Inverse waveform generation result
typedef enum {
    RfInvertOk = 0,
    RfInvertErrUnsupportedModulation,
    RfInvertErrNoSignal,
    RfInvertErrTooComplex,
    RfInvertErrTxNotAllowed,
    RfInvertErrHardware,
} RfInvertResult;

// Forward declaration for the TX engine (full type lives in rf_analyzer_tx.c).
typedef struct RfTxEngine RfTxEngine;
