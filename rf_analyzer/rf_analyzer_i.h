#pragma once

/*
 * Internal application definition for the RF Analyzer.
 *
 * Holds the GUI plumbing (view dispatcher + scene manager), the receive-side
 * engines (scanner and capture) and the in-memory session state (detected
 * signals and the saved frequency list). There is deliberately no transmitter,
 * no TX buffer and no replay/response state anywhere in this application.
 */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "helpers/rf_analyzer_types.h"
#include "helpers/rf_analyzer_scanner.h"
#include "helpers/rf_analyzer_capture.h"
#include "views/rf_analyzer_scan_view.h"

typedef enum {
    RfViewSubmenu,
    RfViewScan,
    RfViewVarList,
    RfViewWidget,
} RfViewId;

typedef struct {
    // GUI
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Submenu* submenu;
    VariableItemList* var_list;
    Widget* widget;
    RfScanView* scan_view;
    NotificationApp* notifications;

    // Receive-side engines
    RfScanner* scanner;
    RfCapture* capture;

    // Session state (in memory only)
    RfSignal signals[RF_ANALYZER_MAX_SIGNALS];
    uint8_t signal_count;

    uint32_t freq_list[RF_ANALYZER_MAX_FREQ_LIST];
    uint8_t freq_list_count;
    uint8_t freq_list_current; // index highlighted in "cycle" mode

    RfScanConfig config;

    // Currently selected signal / analyze target
    uint8_t selected_signal;
    uint32_t analyze_freq;

    // Latest decode result (copied out of the worker callback for display)
    FuriString* last_decode_proto;
    FuriString* last_decode_text;
    volatile bool decode_updated;
    FuriMutex* decode_mutex;

    // Timer that refreshes the live scan view.
    FuriTimer* ui_timer;
} RfAnalyzerApp;

// Session-list helpers (implemented in rf_analyzer.c).
void rf_app_add_signal(RfAnalyzerApp* app, const RfSignal* signal);
void rf_app_clear_signals(RfAnalyzerApp* app);
bool rf_app_add_freq(RfAnalyzerApp* app, uint32_t freq);
void rf_app_remove_freq(RfAnalyzerApp* app, uint8_t index);
