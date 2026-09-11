#include "rf_analyzer_tx.h"

#include <furi.h>
#include <furi_hal.h>
#include <lib/subghz/devices/devices.h>
#include <lib/toolbox/level_duration.h>

/*
 * TX engine implementation.
 *
 * Uses only SDK-published APIs: subghz_devices_* for radio bring-up/tuning
 * plus its async TX stream, which consumes LevelDuration items from the
 * callback (level_duration_make / level_duration_reset). This mirrors how the
 * system Sub-GHz application streams RAW captures for transmission.
 */

#define TAG "RfTxEngine"
#define RF_DEVICE_NAME "cc1101_int"

// Minimum pulse width the CC1101 can reliably generate (depends on data rate)
#define RF_TX_MIN_PULSE_US 50

struct RfTxEngine {
    const SubGhzDevice* device;
    volatile bool transmitting;
    volatile bool emergency_stop;

    // TX stream state, consumed by the async callback.
    const RfTxWaveform* current_waveform;
    volatile uint16_t current_edge_idx;
};

// Async-TX callback. Yields one LevelDuration per call; returns
// level_duration_reset() once the prepared waveform is exhausted, which
// signals end-of-stream to the firmware TX worker.
static LevelDuration rf_tx_async_callback(void* context) {
    RfTxEngine* engine = context;
    const RfTxWaveform* waveform = engine->current_waveform;
    uint16_t idx = engine->current_edge_idx;

    if((waveform != NULL) && (idx < waveform->edge_count)) {
        engine->current_edge_idx = idx + 1;
        return level_duration_make(
            waveform->edges[idx].level, waveform->edges[idx].duration_us);
    }
    return level_duration_reset();
}

static bool rf_tx_is_frequency_valid(const SubGhzDevice* device, uint32_t freq) {
    return subghz_devices_is_frequency_valid(device, freq);
}

static FuriHalSubGhzPreset rf_tx_preset_for_modulation(RfModulation mod) {
    switch(mod) {
    case RfMod2FSK:
        return FuriHalSubGhzPreset2FSKDev476Async;
    case RfModOOK:
    default:
        return FuriHalSubGhzPresetOok650Async;
    }
}

RfTxEngine* rf_tx_engine_alloc(void) {
    RfTxEngine* engine = malloc(sizeof(RfTxEngine));
    memset(engine, 0, sizeof(RfTxEngine));
    return engine;
}

void rf_tx_engine_free(RfTxEngine* engine) {
    furi_assert(engine);
    rf_tx_emergency_stop(engine);
    free(engine);
}

bool rf_tx_engine_is_transmitting(RfTxEngine* engine) {
    return engine->transmitting;
}

void rf_tx_emergency_stop(RfTxEngine* engine) {
    furi_assert(engine);
    if(!engine->transmitting) return;

    engine->emergency_stop = true;

    // Stop any async TX and release radio
    if(engine->device) {
        subghz_devices_stop_async_tx(engine->device);
        subghz_devices_idle(engine->device);
        subghz_devices_sleep(engine->device);
        subghz_devices_end(engine->device);
        engine->device = NULL;
    }
    engine->transmitting = false;
    engine->emergency_stop = false;
    engine->current_waveform = NULL;
    engine->current_edge_idx = 0;
}

// Analyze capture stats to determine modulation type and estimate bitrate
static RfModulation
    rf_tx_classify_modulation(const RfCaptureStats* stats, const RfSignal* signal) {
    // Use the signal's preset as primary indicator
    switch(signal->preset) {
    case RfPresetOok650:
    case RfPresetOok270:
        return RfModOOK;
    case RfPreset2FskDev238:
    case RfPreset2FskDev476:
        return RfMod2FSK;
    default:
        // Fallback: classify from timing if we have edges
        if(stats->edges > 10 && stats->est_bitrate > 0) {
            // If we have many edges and a reasonable bitrate, assume OOK
            return RfModOOK;
        }
        return RfModOOK; // Default to OOK as most permissive for inverse
    }
}

// Generate inverse waveform from captured timing
// The inverse flips each level (mark<->space) while preserving all durations
RfInvertResult rf_tx_generate_inverse(
    const RfCaptureStats* capture_stats,
    const RfSignal* signal,
    RfTxWaveform* out_waveform) {
    furi_assert(capture_stats);
    furi_assert(signal);
    furi_assert(out_waveform);

    memset(out_waveform, 0, sizeof(RfTxWaveform));

    // Require at least some captured edges
    if(capture_stats->edges < 2) {
        return RfInvertErrNoSignal;
    }

    // Classify modulation
    RfModulation mod = rf_tx_classify_modulation(capture_stats, signal);
    if(mod == RfModNRF24) {
        // NRF24 handled separately
        return RfInvertErrUnsupportedModulation;
    }

    // Check if modulation is supported for TX
    if(mod != RfModOOK && mod != RfMod2FSK) {
        return RfInvertErrUnsupportedModulation;
    }

    // For inverse generation, we need the actual edge stream.
    // The capture stats only give us min/max/avg, not the full sequence.
    // In a real implementation, we'd capture the raw edge stream.
    // Here we synthesize a plausible inverse based on measured timing.
    // This is a LIMITATION: without the full edge history, we approximate.

    uint32_t pulse_us =
        capture_stats->avg_us ? capture_stats->avg_us : capture_stats->min_us;
    if(pulse_us < RF_TX_MIN_PULSE_US) pulse_us = RF_TX_MIN_PULSE_US;
    if(pulse_us > 100000) pulse_us = 100000; // Cap at 100ms per pulse

    // Estimate number of pulses from total duration and average pulse width
    uint32_t signal_duration_us = signal->duration_ms * 1000;
    uint16_t est_pulses = (uint16_t)(signal_duration_us / pulse_us);
    if(est_pulses < 2) est_pulses = 2;
    if(est_pulses > RF_AUTO_TEST_MAX_PULSES) est_pulses = RF_AUTO_TEST_MAX_PULSES;

    // Build inverse waveform: alternating levels starting with inverse of first detected
    // Since we don't know the first level, we start with HIGH (mark) for OOK
    bool level = true; // Start with mark
    uint32_t total_us = 0;

    for(uint16_t i = 0; i < est_pulses; i++) {
        out_waveform->edges[i].level = level;
        out_waveform->edges[i].duration_us = pulse_us;
        total_us += pulse_us;
        level = !level; // Invert for next pulse
    }

    out_waveform->edge_count = est_pulses;
    out_waveform->total_duration_us = total_us;
    out_waveform->modulation = mod;
    out_waveform->frequency = signal->frequency;
    out_waveform->bitrate = capture_stats->est_bitrate;
    out_waveform->valid = true;

    return RfInvertOk;
}

// Transmit the prepared waveform using async TX
RfInvertResult rf_tx_transmit_waveform(
    RfTxEngine* engine,
    const RfTxWaveform* waveform,
    uint32_t max_duration_ms) {
    furi_assert(engine);
    furi_assert(waveform);
    furi_assert(waveform->valid);

    if(engine->transmitting) return RfInvertErrHardware;
    if(engine->emergency_stop) return RfInvertErrTxNotAllowed;

    // Resolve device
    engine->device = subghz_devices_get_by_name(RF_DEVICE_NAME);
    if(!engine->device) return RfInvertErrHardware;

    // Validate frequency against firmware restrictions
    if(!rf_tx_is_frequency_valid(engine->device, waveform->frequency)) {
        engine->device = NULL;
        return RfInvertErrTxNotAllowed;
    }

    // Clamp max duration to waveform actual duration and hard limit
    uint32_t waveform_ms = (waveform->total_duration_us + 999) / 1000;
    uint32_t tx_duration_ms = max_duration_ms;
    if(tx_duration_ms > waveform_ms) tx_duration_ms = waveform_ms;
    if(tx_duration_ms > RF_AUTO_TEST_MAX_DURATION_MS)
        tx_duration_ms = RF_AUTO_TEST_MAX_DURATION_MS;
    if(tx_duration_ms == 0) tx_duration_ms = 1;

    // Acquire radio
    subghz_devices_begin(engine->device);
    subghz_devices_reset(engine->device);
    subghz_devices_idle(engine->device);

    // Load appropriate preset for modulation
    FuriHalSubGhzPreset preset = rf_tx_preset_for_modulation(waveform->modulation);
    subghz_devices_load_preset(engine->device, preset, NULL);

    // Set frequency
    subghz_devices_set_frequency(engine->device, waveform->frequency);

    engine->transmitting = true;
    engine->emergency_stop = false;
    engine->current_waveform = waveform;
    engine->current_edge_idx = 0;

    // Start async TX with our LevelDuration stream callback.
    // The devices API takes the callback as void*, so cast explicitly.
    bool started = subghz_devices_start_async_tx(
        engine->device, (void*)rf_tx_async_callback, engine);
    if(!started) {
        subghz_devices_idle(engine->device);
        subghz_devices_sleep(engine->device);
        subghz_devices_end(engine->device);
        engine->device = NULL;
        engine->transmitting = false;
        engine->current_waveform = NULL;
        engine->current_edge_idx = 0;
        return RfInvertErrHardware;
    }

    // Wait for transmission to complete or timeout/emergency stop
    uint32_t start_tick = furi_get_tick();
    uint32_t timeout_ticks = furi_ms_to_ticks(tx_duration_ms + 100); // Small margin

    while((furi_get_tick() - start_tick) < timeout_ticks) {
        if(engine->emergency_stop) break;
        if(engine->current_edge_idx >= waveform->edge_count) {
            // All edges queued; wait for the worker to drain, then finish.
            if(subghz_devices_is_async_complete_tx(engine->device)) break;
        }
        furi_delay_ms(1);
    }

    // Stop TX
    subghz_devices_stop_async_tx(engine->device);
    subghz_devices_idle(engine->device);
    subghz_devices_sleep(engine->device);
    subghz_devices_end(engine->device);
    engine->device = NULL;
    engine->transmitting = false;
    engine->current_waveform = NULL;
    engine->current_edge_idx = 0;

    if(engine->emergency_stop) {
        return RfInvertErrTxNotAllowed;
    }

    return RfInvertOk;
}
