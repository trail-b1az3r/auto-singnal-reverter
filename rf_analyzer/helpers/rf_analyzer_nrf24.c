#include "rf_analyzer_nrf24.h"
#include <furi.h>
#include <furi_hal_nrf24.h>
#include <string.h>

#define TAG "RfNrf24"

static struct {
    volatile bool initialized;
    volatile bool transmitting;
    volatile bool emergency_stop;
    RfNrf24Config config;
    FuriThread* tx_thread;
} s_nrf24 = {0};

// TX thread for NRF24 transmission
static int32_t rf_nrf24_tx_thread(void* context) {
    UNUSED(context);
    const RfTxWaveform* waveform = (const RfTxWaveform*)context;

    if(!waveform || !waveform->valid) {
        s_nrf24.transmitting = false;
        return -1;
    }

    // Convert edge waveform to byte payload for NRF24
    // NRF24 sends packets, so we pack the edge data into 32-byte payloads
    uint8_t payload[32];
    uint16_t edge_idx = 0;
    uint32_t start_tick = furi_get_tick();
    uint32_t max_ticks = furi_ms_to_ticks(1000); // Will be overridden by caller's duration

    // For NRF24, we transmit a series of packets representing the inverse waveform
    // Each packet contains a header + edge data
    while(edge_idx < waveform->edge_count && !s_nrf24.emergency_stop) {
        // Check duration limit
        if(furi_get_tick() - start_tick > max_ticks) break;

        // Build packet: [seq(1)] [edge_count(1)] [edge_data...] 
        memset(payload, 0, sizeof(payload));
        payload[0] = (uint8_t)(edge_idx / 15); // Sequence
        uint8_t edges_in_packet = 0;

        for(uint8_t i = 0; i < 15 && edge_idx < waveform->edge_count; i++, edge_idx++) {
            // Pack each edge as 2 bytes: level(1 bit) + duration_us(15 bits, scaled)
            uint16_t dur = waveform->edges[edge_idx].duration_us;
            if(dur > 32767) dur = 32767;
            uint16_t packed = (waveform->edges[edge_idx].level ? 0x8000 : 0x0000) | (dur & 0x7FFF);
            payload[1 + i * 2] = (uint8_t)(packed >> 8);
            payload[2 + i * 2] = (uint8_t)(packed & 0xFF);
            edges_in_packet++;
        }
        payload[1] = edges_in_packet; // Overwrite with actual count

        // Transmit packet
        bool sent = furi_hal_nrf24_tx(payload, s_nrf24.config.payload_size);
        if(!sent) {
            // TX failed, brief delay and retry
            furi_delay_ms(1);
        }

        // Small inter-packet gap
        furi_delay_ms(1);
    }

    s_nrf24.transmitting = false;
    return 0;
}

bool rf_nrf24_init(const RfNrf24Config* config) {
    if(s_nrf24.initialized) return true;

    if(!config) {
        rf_nrf24_config_defaults(&s_nrf24.config);
    } else {
        s_nrf24.config = *config;
    }

    // Initialize NRF24 hardware
    furi_hal_nrf24_init();

    // Configure
    furi_hal_nrf24_set_channel(s_nrf24.config.channel);
    furi_hal_nrf24_set_tx_address(s_nrf24.config.address);
    furi_hal_nrf24_set_rx_address(s_nrf24.config.address);
    furi_hal_nrf24_set_data_rate(s_nrf24.config.rate);
    furi_hal_nrf24_set_tx_power(s_nrf24.config.power);
    furi_hal_nrf24_set_payload_size(s_nrf24.config.payload_size);

    if(s_nrf24.config.auto_retry) {
        furi_hal_nrf24_set_auto_retr(15, 15); // Max retries
    } else {
        furi_hal_nrf24_set_auto_retr(0, 0); // No auto-retry for clean test
    }

    // Set to TX mode
    furi_hal_nrf24_set_mode(FuriHalNrf24ModeTx);

    s_nrf24.initialized = true;
    s_nrf24.transmitting = false;
    s_nrf24.emergency_stop = false;
    s_nrf24.tx_thread = NULL;

    return true;
}

RfInvertResult rf_nrf24_transmit_inverse(const RfTxWaveform* waveform, uint8_t channel, uint32_t max_duration_ms) {
    if(s_nrf24.transmitting) return RfInvertErrHardware;
    if(s_nrf24.emergency_stop) return RfInvertErrTxNotAllowed;
    if(!waveform || !waveform->valid) return RfInvertErrNoSignal;

    // Update channel if different
    if(channel != s_nrf24.config.channel) {
        s_nrf24.config.channel = channel;
        furi_hal_nrf24_set_channel(channel);
    }

    s_nrf24.emergency_stop = false;
    s_nrf24.transmitting = true;

    // Allocate thread for TX (non-blocking from UI perspective)
    s_nrf24.tx_thread = furi_thread_alloc_ex("Nrf24Tx", 1024, rf_nrf24_tx_thread, (void*)waveform);
    if(!s_nrf24.tx_thread) {
        s_nrf24.transmitting = false;
        return RfInvertErrHardware;
    }

    furi_thread_start(s_nrf24.tx_thread);

    // Wait for completion with timeout
    uint32_t timeout_ticks = furi_ms_to_ticks(max_duration_ms + 100);
    uint32_t start = furi_get_tick();

    while(s_nrf24.transmitting) {
        if(s_nrf24.emergency_stop) break;
        if(furi_get_tick() - start > timeout_ticks) break;
        furi_delay_ms(10);
    }

    // Ensure thread is joined
    if(s_nrf24.tx_thread) {
        furi_thread_join(s_nrf24.tx_thread);
        furi_thread_free(s_nrf24.tx_thread);
        s_nrf24.tx_thread = NULL;
    }

    if(s_nrf24.emergency_stop) {
        return RfInvertErrTxNotAllowed;
    }

    return RfInvertOk;
}

void rf_nrf24_emergency_stop(void) {
    s_nrf24.emergency_stop = true;
    if(s_nrf24.transmitting) {
        furi_hal_nrf24_set_mode(FuriHalNrf24ModePowerDown);
    }
}

bool rf_nrf24_is_transmitting(void) {
    return s_nrf24.transmitting;
}

void rf_nrf24_deinit(void) {
    rf_nrf24_emergency_stop();
    if(s_nrf24.tx_thread) {
        furi_thread_join(s_nrf24.tx_thread);
        furi_thread_free(s_nrf24.tx_thread);
        s_nrf24.tx_thread = NULL;
    }
    if(s_nrf24.initialized) {
        furi_hal_nrf24_deinit();
        s_nrf24.initialized = false;
    }
    memset(&s_nrf24, 0, sizeof(s_nrf24));
}