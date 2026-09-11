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
 *
 * Implementation notes (real SDK APIs only):
 * - Transport is subghz_devices_start_async_tx(), whose callback yields
 *   LevelDuration items (level_duration_make / level_duration_reset from
 *   lib/toolbox/level_duration.h), the same mechanism the system Sub-GHz app
 *   uses for RAW transmission.
 * - Per edge history is not retained by the capture engine, so the inverse is
 *   synthesized from measured min/avg pulse timing (documented limitation).
 */

#include "rf_analyzer_types.h"

typedef struct RfTxEngine RfTxEngine;

RfTxEngine* rf_tx_engine_alloc(void);
void rf_tx_engine_free(RfTxEngine* engine);

RfInvertResult rf_tx_generate_inverse(
    const RfCaptureStats* capture_stats,
    const RfSignal* signal,
    RfTxWaveform* out_waveform);

RfInvertResult rf_tx_transmit_waveform(
    RfTxEngine* engine,
    const RfTxWaveform* waveform,
    uint32_t max_duration_ms);

void rf_tx_emergency_stop(RfTxEngine* engine);

bool rf_tx_engine_is_transmitting(RfTxEngine* engine);
