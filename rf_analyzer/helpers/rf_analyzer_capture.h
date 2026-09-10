#pragma once

/*
 * Fixed-frequency capture & timing analysis.
 *
 * Parks the CC1101 on a single frequency in RX and streams the demodulated
 * level/duration edges out of the radio. From that raw edge stream it derives
 * signal characteristics without transmitting anything:
 *   - edge (transition) count as an activity indicator,
 *   - shortest / longest / mean pulse width,
 *   - an estimated bitrate from the shortest recurring symbol.
 *
 * It deliberately does not attempt full protocol decoding: the firmware's
 * protocol-decoder registry is not part of the public external-app SDK, so
 * this build reports measured timing and clearly marks the protocol as not
 * identified rather than pretending to decode. The modulation is reported from
 * the active demodulation preset.
 *
 * Receive-only: uses subghz_devices_start_async_rx and never the async TX
 * counterpart.
 */

#include "rf_analyzer_types.h"

typedef struct RfCapture RfCapture;

// Snapshot of the timing analysis. All durations are in microseconds.
typedef struct {
    uint32_t edges;       // level transitions seen since start
    uint32_t min_us;      // shortest qualifying pulse (0 if none yet)
    uint32_t max_us;      // longest qualifying pulse
    uint32_t avg_us;      // mean qualifying pulse width
    uint32_t est_bitrate; // bits/sec estimated from the shortest symbol (0 if n/a)
} RfCaptureStats;

RfCapture* rf_capture_alloc(void);
void rf_capture_free(RfCapture* capture);

// Park on `freq` using `preset` and begin sampling. Returns false if the
// frequency is not tunable or a capture is already running.
bool rf_capture_start(RfCapture* capture, uint32_t freq, RfPreset preset);
void rf_capture_stop(RfCapture* capture);
bool rf_capture_is_running(RfCapture* capture);

// Current timing analysis snapshot.
void rf_capture_get_stats(RfCapture* capture, RfCaptureStats* out);

// Human-readable modulation label derived from the active preset.
const char* rf_capture_modulation_hint(RfCapture* capture);
