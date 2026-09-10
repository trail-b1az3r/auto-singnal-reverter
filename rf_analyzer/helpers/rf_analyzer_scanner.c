#include "rf_analyzer_scanner.h"
#include <furi.h>
#include <furi_hal.h>

/*
 * RF-processing logic (receive side)
 * ----------------------------------
 * A "sweep" tunes the CC1101 to each frequency in [freq_start, freq_end] in
 * freq_step increments. On each step we:
 *   1. Program the synthesizer and put the radio into RX.
 *   2. Sample the RSSI repeatedly for `dwell_ms`, tracking the peak value and
 *      how long the signal stayed above the trigger threshold.
 *   3. If the peak crossed the threshold, emit a candidate RfSignal describing
 *      the frequency, peak RSSI, the time we first saw it and the measured
 *      burst duration.
 * The radio is returned to sleep between sweeps and on stop, which releases the
 * RF front-end cleanly.
 */

#define TAG "RfScanner"

// A single RSSI read settles in well under a millisecond; sample at this cadence.
#define RF_SAMPLE_INTERVAL_MS 2

struct RfScanner {
    FuriThread* thread;
    volatile bool running;
    volatile bool stop_requested;

    RfScanConfig config;

    RfScannerCallback callback;
    void* callback_context;

    // Live status, written by the worker and read by the UI (word-sized, so
    // reads are atomic enough for a status display).
    volatile uint32_t current_freq;
    volatile int32_t current_rssi_milli; // dBm * 1000, avoids float tearing
};

const char* rf_preset_name(RfPreset preset) {
    switch(preset) {
    case RfPresetOok650: return "OOK 650kHz";
    case RfPresetOok270: return "OOK 270kHz";
    case RfPreset2FskDev238: return "2-FSK 2.38k";
    case RfPreset2FskDev476: return "2-FSK 47.6k";
    default: return "?";
    }
}

FuriHalSubGhzPreset rf_preset_to_hal(RfPreset preset) {
    switch(preset) {
    case RfPresetOok650: return FuriHalSubGhzPresetOok650Async;
    case RfPresetOok270: return FuriHalSubGhzPresetOok270Async;
    case RfPreset2FskDev238: return FuriHalSubGhzPreset2FSKDev238Async;
    case RfPreset2FskDev476: return FuriHalSubGhzPreset2FSKDev476Async;
    default: return FuriHalSubGhzPresetOok650Async;
    }
}

const char* rf_band_name(RfBand band) {
    switch(band) {
    case RfBand300: return "300-348 MHz";
    case RfBand433: return "387-464 MHz";
    case RfBand868: return "779-928 MHz";
    default: return "?";
    }
}

void rf_band_bounds(RfBand band, uint32_t* start, uint32_t* end) {
    switch(band) {
    case RfBand300: *start = 300000000; *end = 348000000; break;
    case RfBand433: *start = 387000000; *end = 464000000; break;
    case RfBand868: *start = 779000000; *end = 928000000; break;
    default: *start = 433920000; *end = 433920000; break;
    }
}

const char* rf_scan_config_validate(const RfScanConfig* config) {
    if(!config) return "No config";
    if(config->freq_start > config->freq_end) return "Start > End";
    if(config->freq_step < 1000) return "Step < 1 kHz";
    if(config->dwell_ms < 2 || config->dwell_ms > 5000) return "Dwell out of range";
    // Both ends must be tunable by the hardware. is_frequency_valid also
    // enforces the region/regulatory frequency table built into the firmware.
    if(!furi_hal_subghz_is_frequency_valid(config->freq_start)) return "Start not tunable";
    if(!furi_hal_subghz_is_frequency_valid(config->freq_end)) return "End not tunable";
    // Guard against a sweep so large it would never complete usefully.
    uint32_t span = config->freq_end - config->freq_start;
    if(span / config->freq_step > 100000) return "Too many steps";
    return NULL;
}

static void rf_scanner_report(RfScanner* scanner, const RfSignal* signal) {
    if(scanner->callback) {
        scanner->callback(signal, scanner->callback_context);
    }
}

// Sample one frequency for the configured dwell. Returns true if activity was
// seen, filling `out` with the measurement.
static bool rf_scanner_sample_freq(RfScanner* scanner, uint32_t freq, RfSignal* out) {
    // Program synth + path and enter RX. set_frequency_and_path returns the
    // actual programmed frequency, which can differ slightly from the request.
    uint32_t actual = furi_hal_subghz_set_frequency_and_path(freq);
    furi_hal_subghz_rx();

    float peak = -127.0f;
    uint32_t above_start = 0;
    uint32_t above_ms = 0;
    bool triggered = false;

    uint32_t deadline = furi_get_tick() + furi_ms_to_ticks(scanner->config.dwell_ms);
    while(furi_get_tick() < deadline) {
        if(scanner->stop_requested) break;
        float rssi = furi_hal_subghz_get_rssi();
        if(rssi > peak) peak = rssi;

        // Track contiguous time spent above the trigger to estimate burst length.
        if(rssi >= scanner->config.rssi_trigger) {
            if(above_start == 0) above_start = furi_get_tick();
            triggered = true;
        } else if(above_start != 0) {
            above_ms += furi_get_tick() - above_start;
            above_start = 0;
        }

        scanner->current_rssi_milli = (int32_t)(rssi * 1000.0f);
        furi_delay_ms(RF_SAMPLE_INTERVAL_MS);
    }
    if(above_start != 0) above_ms += furi_get_tick() - above_start;

    if(!triggered) return false;

    memset(out, 0, sizeof(RfSignal));
    out->frequency = actual;
    out->rssi = peak;
    out->detected_at = furi_get_tick();
    out->duration_ms = above_ms ? above_ms : 1;
    out->decoded = false;
    out->preset = scanner->config.preset;
    strncpy(out->protocol, "Unknown", RF_ANALYZER_PROTO_NAME_LEN - 1);
    return true;
}

static int32_t rf_scanner_thread(void* context) {
    RfScanner* scanner = context;

    // Bring the radio up, load the requested modulation preset once for the
    // whole sweep. RSSI measurement is preset-dependent (RX bandwidth), so the
    // chosen preset shapes what the sweep is sensitive to.
    furi_hal_subghz_reset();
    furi_hal_subghz_idle();
    furi_hal_subghz_load_preset(rf_preset_to_hal(scanner->config.preset));

    while(!scanner->stop_requested) {
        for(uint32_t f = scanner->config.freq_start; f <= scanner->config.freq_end;
            f += scanner->config.freq_step) {
            if(scanner->stop_requested) break;

            // Skip anything the hardware refuses mid-range instead of aborting
            // the whole sweep.
            if(!furi_hal_subghz_is_frequency_valid(f)) continue;

            scanner->current_freq = f;

            RfSignal sig;
            if(rf_scanner_sample_freq(scanner, f, &sig)) {
                rf_scanner_report(scanner, &sig);
            }
            furi_hal_subghz_idle();
        }
        // Continuous sweep; the UI stops us. Yield briefly between passes.
        furi_delay_ms(1);
    }

    // Release the RF front-end cleanly.
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();

    scanner->running = false;
    return 0;
}

RfScanner* rf_scanner_alloc(void) {
    RfScanner* scanner = malloc(sizeof(RfScanner));
    memset(scanner, 0, sizeof(RfScanner));
    scanner->thread =
        furi_thread_alloc_ex("RfScannerWorker", 2048, rf_scanner_thread, scanner);
    return scanner;
}

void rf_scanner_free(RfScanner* scanner) {
    furi_assert(scanner);
    rf_scanner_stop(scanner);
    furi_thread_free(scanner->thread);
    free(scanner);
}

void rf_scanner_set_callback(RfScanner* scanner, RfScannerCallback cb, void* context) {
    furi_assert(scanner);
    scanner->callback = cb;
    scanner->callback_context = context;
}

bool rf_scanner_start(RfScanner* scanner, const RfScanConfig* config) {
    furi_assert(scanner);
    if(scanner->running) return false;
    if(rf_scan_config_validate(config) != NULL) return false;

    scanner->config = *config;
    scanner->stop_requested = false;
    scanner->running = true;
    scanner->current_freq = config->freq_start;
    scanner->current_rssi_milli = -127000;
    furi_thread_start(scanner->thread);
    return true;
}

void rf_scanner_stop(RfScanner* scanner) {
    furi_assert(scanner);
    if(!scanner->running && !scanner->stop_requested) return;
    scanner->stop_requested = true;
    furi_thread_join(scanner->thread);
    scanner->running = false;
    // Clear the flag so the thread can be restarted and a repeat stop is a no-op.
    scanner->stop_requested = false;
}

bool rf_scanner_is_running(RfScanner* scanner) {
    return scanner->running;
}

uint32_t rf_scanner_current_freq(RfScanner* scanner) {
    return scanner->current_freq;
}

float rf_scanner_current_rssi(RfScanner* scanner) {
    return (float)scanner->current_rssi_milli / 1000.0f;
}
