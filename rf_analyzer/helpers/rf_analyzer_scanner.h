#pragma once

/*
 * RSSI-sweep scanner.
 *
 * Runs a background thread that steps across the configured frequency range,
 * parks the CC1101 in RX on each step, reads the received signal strength and
 * reports any frequency whose RSSI rises above the trigger threshold as a
 * candidate signal.
 *
 * This module only ever puts the radio into RX or idle/sleep. It contains no
 * transmit calls.
 */

#include "rf_analyzer_types.h"

typedef struct RfScanner RfScanner;

// Invoked (from the scanner thread) whenever a candidate signal is detected.
// The RfSignal is owned by the caller for the duration of the call only.
typedef void (*RfScannerCallback)(const RfSignal* signal, void* context);

RfScanner* rf_scanner_alloc(void);
void rf_scanner_free(RfScanner* scanner);

void rf_scanner_set_callback(RfScanner* scanner, RfScannerCallback cb, void* context);

// Validate a scan configuration against the hardware limits.
// Returns NULL when the config is usable, otherwise a static human-readable
// reason suitable for display.
const char* rf_scan_config_validate(const RfScanConfig* config);

// Start sweeping. Returns false (and does nothing) if the config is invalid or
// a scan is already running. Safe to call from the UI thread.
bool rf_scanner_start(RfScanner* scanner, const RfScanConfig* config);

// Stop sweeping and release the radio. Blocks until the worker thread has
// parked the radio in sleep. Safe to call when not running.
void rf_scanner_stop(RfScanner* scanner);

bool rf_scanner_is_running(RfScanner* scanner);

// Live status for the scan view (approximate; updated as the sweep progresses).
uint32_t rf_scanner_current_freq(RfScanner* scanner);
float rf_scanner_current_rssi(RfScanner* scanner);
