#pragma once

/*
 * Shared data types for the RF Analyzer.
 *
 * The application is strictly receive-only. These structures describe what the
 * radio *observed* (RSSI, timing, decoded protocol) and how the user has
 * configured the scan. Nothing here describes a transmission — there is no
 * transmit path in this application.
 */

#include <furi.h>
#include <furi_hal.h>

// Hard limits. The Flipper has a small heap, so the session list is capped and
// old entries are evicted rather than allowed to exhaust memory.
#define RF_ANALYZER_MAX_SIGNALS      32
#define RF_ANALYZER_MAX_FREQ_LIST    32
#define RF_ANALYZER_PROTO_NAME_LEN   32

// Sub-GHz bands the CC1101 in the Flipper Zero can tune. These are the coarse
// ranges the firmware's frequency table allows; the actual per-frequency
// legality check is delegated to furi_hal_subghz_is_frequency_valid().
typedef enum {
    RfBand300 = 0, // 300.000 - 348.000 MHz
    RfBand433,     // 387.000 - 464.000 MHz
    RfBand868,     // 779.000 - 928.000 MHz
    RfBandCount,
} RfBand;

// Modulation preset used when parking on a frequency for decode. Maps onto the
// firmware's FuriHalSubGhzPreset values.
typedef enum {
    RfPresetOok650 = 0, // OOK, 650 kHz RX bandwidth (most common for remotes)
    RfPresetOok270,     // OOK, 270 kHz RX bandwidth
    RfPreset2FskDev238, // 2-FSK, 2.38 kHz deviation
    RfPreset2FskDev476, // 2-FSK, 47.6 kHz deviation
    RfPresetCount,
} RfPreset;

// One observed candidate signal. Populated entirely from receive-side data.
typedef struct {
    uint32_t frequency;                       // Hz
    float rssi;                               // dBm (peak observed during dwell)
    uint32_t detected_at;                     // furi_get_tick() when first seen
    uint32_t duration_ms;                     // burst length while above threshold
    bool decoded;                             // true if a protocol was identified
    RfPreset preset;                          // modulation preset active at detection
    char protocol[RF_ANALYZER_PROTO_NAME_LEN]; // decoded protocol name or "Unknown"
    uint64_t data;                            // decoded payload (0 if not decoded)
    uint32_t data_bits;                       // payload bit count (0 if not decoded)
} RfSignal;

// User-configurable scan parameters. Range is validated against the hardware
// before a scan starts.
typedef struct {
    uint32_t freq_start;   // Hz, inclusive
    uint32_t freq_end;     // Hz, inclusive
    uint32_t freq_step;    // Hz between sample points
    float    rssi_trigger; // dBm; a sample above this counts as activity
    uint32_t dwell_ms;     // time spent sampling each frequency
    RfPreset preset;       // modulation preset used while scanning/decoding
} RfScanConfig;

// Human-readable helpers (implemented in rf_analyzer_scanner.c).
const char* rf_preset_name(RfPreset preset);
FuriHalSubGhzPreset rf_preset_to_hal(RfPreset preset);
const char* rf_band_name(RfBand band);
void rf_band_bounds(RfBand band, uint32_t* start, uint32_t* end);
