#pragma once

/*
 * Custom "live scan" view.
 *
 * Renders the sweep status: the frequency currently being sampled, live RSSI,
 * the number of candidate signals found this session and — prominently — the
 * radio state. Because this application never transmits, the state indicator is
 * hard-wired to show RX / IDLE only; there is no TX state to display.
 */

#include <gui/view.h>

typedef struct RfScanView RfScanView;

// Invoked when the user presses OK (toggle pause) or Back (leave scan).
typedef void (*RfScanViewCallback)(void* context);

RfScanView* rf_scan_view_alloc(void);
void rf_scan_view_free(RfScanView* view);
View* rf_scan_view_get_view(RfScanView* view);

void rf_scan_view_set_ok_callback(RfScanView* view, RfScanViewCallback cb, void* context);
void rf_scan_view_set_back_callback(RfScanView* view, RfScanViewCallback cb, void* context);

// Update live status. `running` false renders "IDLE (RX armed)".
void rf_scan_view_set_status(
    RfScanView* view,
    uint32_t freq,
    float rssi,
    bool running,
    uint8_t detected);
void rf_scan_view_set_range(RfScanView* view, uint32_t start, uint32_t end);
