#pragma once

/*
 * NRF24 support for Auto Inverse Test.
 *
 * Provides NRF24L01+ transmit capability for inverse waveform testing.
 * Only active when user explicitly enables NRF24 mode in Auto Test config.
 * Uses Flipper's furi_hal_nrf24 API.
 */

#include "rf_analyzer_types.h"
#include <furi_hal_nrf24.h>

// NRF24 configuration for test mode
typedef struct {
    uint8_t channel;          // RF channel (0-125), 2.4 GHz + channel MHz
    uint8_t address[5];       // TX address (default: 0xE7E7E7E7E7)
    uint8_t payload_size;     // Payload size in bytes (1-32)
    FuriHalNrf24DataRate rate; // Data rate
    FuriHalNrf24TxPower power; // TX power
    bool auto_retry;          // Enable auto-retry (for testing only)
} RfNrf24Config;

// Default NRF24 config for inverse test
static inline void rf_nrf24_config_defaults(RfNrf24Config* config) {
    config->channel = 2;
    config->address[0] = 0xE7;
    config->address[1] = 0xE7;
    config->address[2] = 0xE7;
    config->address[3] = 0xE7;
    config->address[4] = 0xE7;
    config->payload_size = 32;
    config->rate = FuriHalNrf24DataRate1M;
    config->power = FuriHalNrf24TxPower0dBm;
    config->auto_retry = false;
}

// Initialize NRF24 for TX
bool rf_nrf24_init(const RfNrf24Config* config);

// Transmit inverse waveform via NRF24
// Converts the edge-based waveform to NRF24 packets
RfInvertResult rf_nrf24_transmit_inverse(const RfTxWaveform* waveform, uint8_t channel, uint32_t max_duration_ms);

// Emergency stop NRF24 transmission
void rf_nrf24_emergency_stop(void);

// Check if NRF24 is currently transmitting
bool rf_nrf24_is_transmitting(void);

// Deinitialize NRF24
void rf_nrf24_deinit(void);