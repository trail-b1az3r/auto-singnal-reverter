#pragma once

/*
 * TX Engine — controlled inverse waveform transmission for Auto Inverse Test.
 *
 * This module implements the transmit side of the Auto Inverse Test feature.
 * It is ONLY used when the user explicitly enables Auto Inverse Test, configures
 * a test frequency, and the signal matches all criteria. It respects all
 * firmware frequency validity and power restrictions.
 *
 * The waveform is a logical inverse (level flipped, timing preserved) of the
 * captured signal edges. Only OOK/ASK and simple 2-FSK are supported for
 * inverse generation; complex/unknown modulations are rejected.
 */

#include "rf_analyzer_types.h"

struct RfTxEngine {
    const SubGhzDevice* device;
    volatile bool transmitting;
    volatile bool emergency_stop;
};

RfTxEngine* rf_tx_engine_alloc(void);
void rf_tx_engine_free(RfTxEngine* engine);

RfInvertResult rf_tx_generate_inverse(
    const RfCaptureStats* capture_stats,
    const RfSignal* signal,
    RfTxWaveform* out_waveform);

RfInvertResult rf_tx_transmit_waveform(RfTxEngine* engine, const RfTxWaveform* waveform, uint32_t max_duration_ms);
void rf_tx_emergency_stop(RfTxEngine* engine);

bool rf_tx_engine_is_transmitting(RfTxEngine* engine);

// Internal helpers
static inline bool rf_tx_is_frequency_valid(const SubGhzDevice* device, uint32_t freq) {
    return subghz_devices_is_frequency_valid(device, freq);
}

static inline FuriHalSubGhzPreset rf_tx_preset_for_modulation(RfModulation mod) {
    switch(mod) {
    case RfModOOK:
        return FuriHalSubGhzPresetOok650Async;
    case RfMod2FSK:
        return FuriHalSubGhzPreset2FSKDev476Async;
    default:
        return FuriHalSubGhzPresetOok650Async;
    }
}