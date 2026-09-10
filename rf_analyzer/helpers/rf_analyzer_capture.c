#include "rf_analyzer_capture.h"

#include <furi.h>
#include <furi_hal.h>
#include <lib/subghz/devices/devices.h>

/*
 * Capture pipeline
 * ----------------
 * subghz_devices async RX delivers (level, duration) events straight to our
 * callback, which runs in interrupt context. We keep the per-edge work tiny and
 * allocation-free: bump counters and update min/max/sum for pulses that fall in
 * a plausible Sub-GHz symbol window. The UI thread reads a consistent snapshot
 * via rf_capture_get_stats().
 *
 * "Qualifying" pulse window: ignore sub-microsecond glitches and multi-second
 * gaps so the min/bitrate estimate reflects real modulation symbols rather than
 * noise spikes or dead air.
 */

#define TAG "RfCapture"
#define RF_DEVICE_NAME "cc1101_int"

#define RF_PULSE_MIN_US 50      // below this = glitch/noise, ignore
#define RF_PULSE_MAX_US 100000  // above this = inter-frame gap, ignore

struct RfCapture {
    const SubGhzDevice* device;
    volatile bool running;
    RfPreset preset;

    // Updated from interrupt context. 32-bit scalar writes are atomic on the
    // target, so the UI reads a coherent-enough snapshot without a lock.
    volatile uint32_t edges;
    volatile uint32_t min_us;
    volatile uint32_t max_us;
    volatile uint32_t sum_us;
    volatile uint32_t count; // qualifying pulses (for the mean)
};

// Interrupt-context async-RX callback. Observes only; cannot transmit.
static void rf_capture_rx_edge(bool level, uint32_t duration, void* context) {
    UNUSED(level);
    RfCapture* capture = context;
    capture->edges++;

    if(duration < RF_PULSE_MIN_US || duration > RF_PULSE_MAX_US) return;

    if(capture->min_us == 0 || duration < capture->min_us) capture->min_us = duration;
    if(duration > capture->max_us) capture->max_us = duration;
    capture->sum_us += duration;
    capture->count++;
}

RfCapture* rf_capture_alloc(void) {
    RfCapture* capture = malloc(sizeof(RfCapture));
    memset(capture, 0, sizeof(RfCapture));
    return capture;
}

void rf_capture_free(RfCapture* capture) {
    furi_assert(capture);
    rf_capture_stop(capture);
    free(capture);
}

bool rf_capture_start(RfCapture* capture, uint32_t freq, RfPreset preset) {
    furi_assert(capture);
    if(capture->running) return false;

    capture->device = subghz_devices_get_by_name(RF_DEVICE_NAME);
    if(!capture->device) return false;
    if(!subghz_devices_is_frequency_valid(capture->device, freq)) return false;

    // Reset analysis state.
    capture->edges = 0;
    capture->min_us = 0;
    capture->max_us = 0;
    capture->sum_us = 0;
    capture->count = 0;
    capture->preset = preset;

    // Acquire radio, select modulation, tune, then stream RX edges to us.
    subghz_devices_begin(capture->device);
    subghz_devices_reset(capture->device);
    subghz_devices_idle(capture->device);
    subghz_devices_load_preset(capture->device, rf_preset_to_hal(preset), NULL);
    subghz_devices_set_frequency(capture->device, freq);
    subghz_devices_start_async_rx(capture->device, rf_capture_rx_edge, capture);

    capture->running = true;
    return true;
}

void rf_capture_stop(RfCapture* capture) {
    furi_assert(capture);
    if(!capture->running) return;

    subghz_devices_stop_async_rx(capture->device);
    subghz_devices_idle(capture->device);
    subghz_devices_sleep(capture->device);
    subghz_devices_end(capture->device);

    capture->running = false;
}

bool rf_capture_is_running(RfCapture* capture) {
    return capture->running;
}

void rf_capture_get_stats(RfCapture* capture, RfCaptureStats* out) {
    furi_assert(capture);
    furi_assert(out);
    // Snapshot the volatile fields once.
    uint32_t cnt = capture->count;
    uint32_t sum = capture->sum_us;
    out->edges = capture->edges;
    out->min_us = capture->min_us;
    out->max_us = capture->max_us;
    out->avg_us = cnt ? (sum / cnt) : 0;
    // The shortest symbol approximates one bit period for simple OOK/FSK keying.
    out->est_bitrate = capture->min_us ? (1000000UL / capture->min_us) : 0;
}

const char* rf_capture_modulation_hint(RfCapture* capture) {
    switch(capture->preset) {
    case RfPresetOok650:
    case RfPresetOok270:
        return "OOK/ASK";
    case RfPreset2FskDev238:
    case RfPreset2FskDev476:
        return "2-FSK";
    default:
        return "?";
    }
}
