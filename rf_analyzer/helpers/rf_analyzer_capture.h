#pragma once

/*
 * Fixed-frequency capture & decode.
 *
 * Parks the CC1101 on a single frequency in RX and feeds the demodulated
 * level/duration stream into the firmware's Sub-GHz protocol decoders. When a
 * known protocol is recognised the caller is notified with the protocol name
 * and a textual description. Signals that never decode are surfaced by the
 * scene as "Unknown / cannot decode".
 *
 * Receive-only: this uses furi_hal_subghz_start_async_rx and never the async TX
 * counterpart.
 */

#include "rf_analyzer_types.h"

typedef struct RfCapture RfCapture;

// Called (from the worker thread) when a protocol decodes. Strings are valid
// only for the duration of the call.
typedef void (*RfCaptureCallback)(
    const char* protocol,
    const char* details,
    void* context);

RfCapture* rf_capture_alloc(void);
void rf_capture_free(RfCapture* capture);

void rf_capture_set_callback(RfCapture* capture, RfCaptureCallback cb, void* context);

// Park on `freq` using `preset` and begin decoding. Returns false if the
// frequency is not tunable or a capture is already running.
bool rf_capture_start(RfCapture* capture, uint32_t freq, RfPreset preset);
void rf_capture_stop(RfCapture* capture);
bool rf_capture_is_running(RfCapture* capture);

// Number of raw level transitions seen since start (activity indicator even
// when nothing decodes).
uint32_t rf_capture_edge_count(RfCapture* capture);
