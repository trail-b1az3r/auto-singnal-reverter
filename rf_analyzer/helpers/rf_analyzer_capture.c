#include "rf_analyzer_capture.h"

#include <furi.h>
#include <furi_hal.h>

#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/protocols/registry.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/types.h>

/*
 * Decode pipeline
 * ---------------
 * furi_hal async RX  ->  SubGhzWorker  ->  SubGhzReceiver  ->  protocol decoders
 *
 *  - furi_hal_subghz_start_async_rx() delivers (level, duration) capture events
 *    to subghz_worker_rx_callback.
 *  - The worker buffers them and, on its own thread, hands each pair to
 *    subghz_receiver_decode (wired via the pair callback).
 *  - The receiver runs every registered decoder in parallel; when one reaches
 *    a complete, valid frame it fires our rx callback with the decoder.
 * We keep the filter at "Decodable" so only real, framed protocols are
 * reported — raw noise does not masquerade as a decode.
 */

#define TAG "RfCapture"

// Standard location of the manufacturer keystore. Loading it lets some
// protocols (e.g. rolling-code remotes) be named; absence is non-fatal.
#define RF_KEYSTORE_PATH "/ext/subghz/assets/keeloq_mfcodes"

struct RfCapture {
    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzWorker* worker;

    volatile bool running;
    volatile uint32_t edge_count;

    RfCaptureCallback callback;
    void* callback_context;
};

// Raw async-RX tap: runs in interrupt context. Counts level transitions as an
// activity indicator, then forwards the sample to the Sub-GHz worker exactly as
// the firmware expects. It only observes -- it cannot and does not transmit.
static void rf_capture_rx_raw(bool level, uint32_t duration, void* context) {
    RfCapture* capture = context;
    capture->edge_count++;
    subghz_worker_rx_callback(level, duration, capture->worker);
}

// Fired by the receiver when any decoder completes a frame.
static void rf_capture_rx_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    RfCapture* capture = context;

    const char* name = "Unknown";
    if(decoder_base && decoder_base->protocol && decoder_base->protocol->name) {
        name = decoder_base->protocol->name;
    }

    // Ask the decoder for a human-readable rendering of the captured frame.
    FuriString* text = furi_string_alloc();
    subghz_protocol_decoder_base_get_string(decoder_base, text);

    if(capture->callback) {
        capture->callback(name, furi_string_get_cstr(text), capture->callback_context);
    }

    furi_string_free(text);

    // Reset so the same decoder can catch the next repetition.
    subghz_receiver_reset(receiver);
}

RfCapture* rf_capture_alloc(void) {
    RfCapture* capture = malloc(sizeof(RfCapture));
    memset(capture, 0, sizeof(RfCapture));

    capture->environment = subghz_environment_alloc();
    // Best-effort keystore load; ignore the result so a missing file does not
    // stop plain protocol identification from working.
    subghz_environment_load_keystore(capture->environment, RF_KEYSTORE_PATH);
    subghz_environment_set_protocol_registry(
        capture->environment, (void*)&subghz_protocol_registry);

    capture->receiver = subghz_receiver_alloc_init(capture->environment);
    subghz_receiver_set_filter(capture->receiver, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_rx_callback(capture->receiver, rf_capture_rx_callback, capture);

    capture->worker = subghz_worker_alloc();
    subghz_worker_set_overrun_callback(
        capture->worker, (SubGhzWorkerOverrunCallback)subghz_receiver_reset);
    subghz_worker_set_pair_callback(
        capture->worker, (SubGhzWorkerPairCallback)subghz_receiver_decode);
    subghz_worker_set_context(capture->worker, capture->receiver);

    return capture;
}

void rf_capture_free(RfCapture* capture) {
    furi_assert(capture);
    rf_capture_stop(capture);
    subghz_receiver_free(capture->receiver);
    subghz_environment_free(capture->environment);
    subghz_worker_free(capture->worker);
    free(capture);
}

void rf_capture_set_callback(RfCapture* capture, RfCaptureCallback cb, void* context) {
    furi_assert(capture);
    capture->callback = cb;
    capture->callback_context = context;
}

bool rf_capture_start(RfCapture* capture, uint32_t freq, RfPreset preset) {
    furi_assert(capture);
    if(capture->running) return false;
    if(!furi_hal_subghz_is_frequency_valid(freq)) return false;

    capture->edge_count = 0;
    subghz_receiver_reset(capture->receiver);

    // Bring up the radio, select modulation, tune, then start streaming RX
    // samples into the worker.
    furi_hal_subghz_reset();
    furi_hal_subghz_idle();
    furi_hal_subghz_load_preset(rf_preset_to_hal(preset));
    furi_hal_subghz_set_frequency_and_path(freq);

    subghz_worker_start(capture->worker);
    furi_hal_subghz_start_async_rx(rf_capture_rx_raw, capture);

    capture->running = true;
    return true;
}

void rf_capture_stop(RfCapture* capture) {
    furi_assert(capture);
    if(!capture->running) return;

    furi_hal_subghz_stop_async_rx();
    subghz_worker_stop(capture->worker);

    // Release the RF front-end.
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();

    capture->running = false;
}

bool rf_capture_is_running(RfCapture* capture) {
    return capture->running;
}

uint32_t rf_capture_edge_count(RfCapture* capture) {
    return capture->edge_count;
}
