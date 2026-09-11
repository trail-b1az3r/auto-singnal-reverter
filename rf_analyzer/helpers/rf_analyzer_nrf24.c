#include "rf_analyzer_nrf24.h"

#include <furi.h>

/*
 * NRF24 stub implementation.
 *
 * Rationale: there is no furi_hal_nrf24 (or equivalent) header in the official
 * FAP SDK, so an external app cannot drive an NRF24 radio through published
 * HAL calls. Catalog NRF24 apps bundle their own GPIO bit-bang driver for an
 * externally wired module instead. Until such a driver is vendored here, the
 * transmit path stays a stub that reports "unsupported" rather than inventing
 * SDK calls. The Settings UI (mode/channel) is retained so the configuration
 * plumbing is in place for that future driver.
 */

#define TAG "RfNrf24"

static struct {
    volatile bool initialized;
    volatile bool transmitting;
    volatile bool emergency_stop;
    RfNrf24Config config;
} s_nrf24 = {0};

void rf_nrf24_config_defaults(RfNrf24Config* config) {
    furi_assert(config);
    config->channel = 2;
    config->address[0] = 0xE7;
    config->address[1] = 0xE7;
    config->address[2] = 0xE7;
    config->address[3] = 0xE7;
    config->address[4] = 0xE7;
    config->payload_size = 32;
    config->data_rate = 0;
    config->tx_power_dbm = 0;
    config->auto_retry = false;
}

bool rf_nrf24_init(const RfNrf24Config* config) {
    if(s_nrf24.initialized) return true;

    if(config == NULL) {
        rf_nrf24_config_defaults(&s_nrf24.config);
    } else {
        s_nrf24.config = *config;
    }

    s_nrf24.initialized = true;
    s_nrf24.transmitting = false;
    s_nrf24.emergency_stop = false;
    return true;
}

RfInvertResult rf_nrf24_transmit_inverse(
    const RfTxWaveform* waveform,
    uint8_t channel,
    uint32_t max_duration_ms) {
    UNUSED(channel);
    UNUSED(max_duration_ms);
    if(s_nrf24.emergency_stop) return RfInvertErrTxNotAllowed;
    if((waveform == NULL) || !waveform->valid) return RfInvertErrNoSignal;

    // Documented limitation: no NRF24 HAL in the official SDK. Report the
    // mode as unsupported instead of fabricating radio calls.
    return RfInvertErrUnsupportedModulation;
}

void rf_nrf24_emergency_stop(void) {
    s_nrf24.emergency_stop = true;
    s_nrf24.transmitting = false;
}

bool rf_nrf24_is_transmitting(void) {
    return s_nrf24.transmitting;
}

void rf_nrf24_deinit(void) {
    rf_nrf24_emergency_stop();
    s_nrf24.initialized = false;
    s_nrf24.emergency_stop = false;
}
