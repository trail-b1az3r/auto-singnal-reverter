#include "rf_analyzer_scan_view.h"
#include <gui/elements.h>
#include <furi.h>

struct RfScanView {
    View* view;
    RfScanViewCallback ok_cb;
    void* ok_ctx;
    RfScanViewCallback back_cb;
    void* back_ctx;
};

typedef struct {
    uint32_t freq;
    float rssi;
    bool running;
    uint8_t detected;
    uint32_t range_start;
    uint32_t range_end;
} RfScanViewModel;

// Draw an RSSI bar from a dBm value. Typical Sub-GHz RSSI spans roughly
// -110 dBm (floor) to -30 dBm (strong), so map that window to the bar width.
static uint8_t rssi_to_bar(float rssi, uint8_t max_px) {
    float lo = -110.0f, hi = -30.0f;
    if(rssi < lo) rssi = lo;
    if(rssi > hi) rssi = hi;
    return (uint8_t)(((rssi - lo) / (hi - lo)) * max_px);
}

static void rf_scan_view_draw(Canvas* canvas, void* _model) {
    RfScanViewModel* m = _model;
    canvas_clear(canvas);

    // Header + RX/TX status. Always RX: this build cannot transmit.
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Sub-GHz Scan");
    canvas_set_font(canvas, FontSecondary);
    if(m->running) {
        elements_bold_rounded_frame(canvas, 90, 1, 36, 12);
        canvas_draw_str(canvas, 98, 10, "RX");
    } else {
        canvas_draw_frame(canvas, 90, 1, 36, 12);
        canvas_draw_str(canvas, 94, 10, "IDLE");
    }

    // Current frequency.
    char line[40];
    uint32_t mhz = m->freq / 1000000;
    uint32_t khz = (m->freq % 1000000) / 1000;
    snprintf(line, sizeof(line), "%lu.%03lu MHz", (unsigned long)mhz, (unsigned long)khz);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 28, line);

    // RSSI value + bar.
    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "RSSI %d dBm", (int)m->rssi);
    canvas_draw_str(canvas, 2, 42, line);
    canvas_draw_frame(canvas, 2, 46, 100, 7);
    canvas_draw_box(canvas, 3, 47, rssi_to_bar(m->rssi, 98), 5);

    // Detected count + hint.
    snprintf(line, sizeof(line), "Found: %u", m->detected);
    canvas_draw_str(canvas, 2, 62, line);
    canvas_draw_str(canvas, 60, 62, m->running ? "OK:Pause Back:Exit" : "OK:Resume");
}

static bool rf_scan_view_input(InputEvent* event, void* context) {
    RfScanView* view = context;
    if(event->type != InputTypeShort) return false;

    if(event->key == InputKeyOk) {
        if(view->ok_cb) view->ok_cb(view->ok_ctx);
        return true;
    }
    if(event->key == InputKeyBack) {
        if(view->back_cb) view->back_cb(view->back_ctx);
        return true;
    }
    return false;
}

RfScanView* rf_scan_view_alloc(void) {
    RfScanView* view = malloc(sizeof(RfScanView));
    memset(view, 0, sizeof(RfScanView));
    view->view = view_alloc();
    view_set_context(view->view, view);
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(RfScanViewModel));
    view_set_draw_callback(view->view, rf_scan_view_draw);
    view_set_input_callback(view->view, rf_scan_view_input);
    return view;
}

void rf_scan_view_free(RfScanView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* rf_scan_view_get_view(RfScanView* view) {
    return view->view;
}

void rf_scan_view_set_ok_callback(RfScanView* view, RfScanViewCallback cb, void* context) {
    view->ok_cb = cb;
    view->ok_ctx = context;
}

void rf_scan_view_set_back_callback(RfScanView* view, RfScanViewCallback cb, void* context) {
    view->back_cb = cb;
    view->back_ctx = context;
}

void rf_scan_view_set_status(
    RfScanView* view,
    uint32_t freq,
    float rssi,
    bool running,
    uint8_t detected) {
    with_view_model(
        view->view,
        RfScanViewModel * m,
        {
            m->freq = freq;
            m->rssi = rssi;
            m->running = running;
            m->detected = detected;
        },
        true);
}

void rf_scan_view_set_range(RfScanView* view, uint32_t start, uint32_t end) {
    with_view_model(
        view->view,
        RfScanViewModel * m,
        {
            m->range_start = start;
            m->range_end = end;
        },
        false);
}
