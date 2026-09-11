#pragma once

/*
 * NRF24 support for Auto Inverse Test.
 *
 * Configuration + status plumbing for an NRF24L01+ test path. The channel and
 * mode settings are functional UI; actual packet transmission needs an
 * external NRF24 module plus a GPIO driver bundled with the app (the approach
 * used by the catalog NRF24 scanner/mousejack apps).
 *
 * LIMITATION: the official Flipper Zero FAP SDK does not publish an
 * furi_hal_nrf24-style HAL for external apps, so this build intentionally does
 * NOT claim on-air NRF24 transmission. rf_nrf24_transmit_inverse() returns
 * RfInvertErrUnsupportedModulation until such a driver is bundled, and the
 * scene surfaces that status instead of transmitting.
 */

#include "rf_analyzer_types.h"

// NRF24 configuration for test mode (plain types only — no HAL dependency).
typedef struct {
    uint8_t channel; // RF channel (0-125)
    uint8_t address[5]; // TX address (default: 0xE7E7E7E7E7)
    uint8_t payload_size; // Payload size in bytes (1-32)
    uint8_t data_rate; // 0 = 1 Mbps, 1 = 2 Mbps, 2 = 250 kbps
    int8_t tx_power_dbm; // TX power in dBm (0, -6, -12, -18)
    bool auto_retry; // Enable auto-retry (for testing only)
} RfNrf24Config;

// Default NRF24 config for inverse test
void rf_nrf24_config_defaults(RfNrf24Config* config);

// Initialize NRF24 for TX (stores config; returns true when usable).
bool rf_nrf24_init(const RfNrf24Config* config);

// Transmit inverse waveform via NRF24.
// Currently a documented stub: returns RfInvertErrUnsupportedModulation
// because the official SDK exposes no NRF24 HAL for external apps.
RfInvertResult rf_nrf24_transmit_inverse(
    const RfTxWaveform* waveform,
    uint8_t channel,
    uint32_t max_duration_ms);

// Emergency stop NRF24 transmission
void rf_nrf24_emergency_stop(void);

// Check if NRF24 is currently transmitting
bool rf_nrf24_is_transmitting(void);

// Deinitialize NRF24
void rf_nrf24_deinit(void);
