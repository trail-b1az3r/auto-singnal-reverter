# RF scanning & analysis architecture

This document explains the receive-side signal path and how the application is
structured. The design goal is: **discover activity quickly, then let the user
inspect one signal at a time in depth — without ever transmitting and without
blocking the UI.**

## Threading model

```
 GUI thread (view dispatcher + scene manager)
   │  starts/stops
   ├──────────────► Scanner thread (RfScanner)      ── RSSI sweep
   │                    reports detections via callback ─┐
   │                                                     ▼
   │                                            rf_app_add_signal() (session list)
   │
   ├──────────────► Capture (subghz_devices async RX) ── timing on one freq
   │                    edge callback updates RfCaptureStats (interrupt ctx)
   │
   └──────────────► UI timer (FuriTimer)  ── pulls live status @ 4–10 Hz,
                        updates view models (lock-protected)
```

The GUI thread never does RF work directly, so the interface stays responsive
while a sweep or a decode is running. The scanner and the capture pipeline are
never active at the same time — each acquires the single CC1101 while its scene
is on screen and releases it on scene exit.

## 1. Frequency discovery — the RSSI sweep (`helpers/rf_analyzer_scanner.c`)

A background thread steps across `[freq_start, freq_end]` in `freq_step`
increments. For each step it:

1. Programs the synthesizer and RF path with
   `subghz_devices_set_frequency()` and enters RX
   (`subghz_devices_set_rx()`).
2. Samples `subghz_devices_get_rssi()` every ~2 ms for `dwell_ms`, tracking:
   - the **peak** RSSI (reported as signal strength), and
   - the **contiguous time above** the `rssi_trigger` threshold (reported as the
     burst *duration*).
3. If the peak crossed the threshold, emits an `RfSignal` (frequency, peak RSSI,
   first-seen timestamp, measured duration, active preset) to the app callback.

The chosen modulation preset sets the CC1101 RX bandwidth, which shapes what the
sweep is sensitive to. Between steps and on stop the radio is returned to
`idle`/`sleep`, releasing the front-end.

**Why RSSI first?** A full protocol decoder cannot run on every frequency in
real time. A cheap RSSI energy sweep finds *where* activity is; the decoder is
then pointed at a single candidate.

### Robustness

- Config is validated (`rf_scan_config_validate`) against a supported-band table
  and sane step/dwell/span limits before a scan starts; invalid ranges surface a
  readable reason instead of scanning.
- Frequencies the hardware rejects mid-range (`subghz_devices_is_frequency_valid`)
  are skipped, not fatal.
- The session list is capped (`RF_ANALYZER_MAX_SIGNALS`); nearby detections are
  merged (keeping the strongest RSSI) and the oldest entry is evicted when full,
  so memory use is bounded.

## 2. Signal analysis — capture & timing (`helpers/rf_analyzer_capture.c`)

To analyze, the receiver parks on one frequency and streams the demodulated
level/duration edges out of the radio via `subghz_devices_start_async_rx()`:

```
subghz_devices async RX ──► rf_capture_rx_edge (interrupt context)
                                    │  bumps counters, updates min/max/sum
                                    ▼
                          RfCaptureStats  ◄── read by the Analyze scene (UI thread)
```

For each qualifying pulse (inside a plausible symbol window, filtering out
sub-microsecond glitches and multi-second gaps) the callback updates the edge
count and the shortest / longest / summed pulse width. From that the app derives
the mean pulse width and an estimated bitrate (`1e6 / shortest symbol`). The
per-edge work is tiny and allocation-free so it is safe in interrupt context.

**Modulation** is reported from the active demodulation preset (OOK/ASK vs
2-FSK). **Full protocol decoding is intentionally not attempted**: the firmware's
protocol-decoder registry is not part of the public external-app SDK, so instead
of pretending to decode, the Analyze screen shows the measured timing and states
plainly that the protocol is *not identified*. This satisfies the requirement to
clearly indicate when a signal cannot be decoded.

Captured data lives **only in memory** for the session. Nothing is transmitted.

## 3. Controlled testing — reframed as receive-side inspection

The original brief asked for an "inverse/response" transmit test mode. That is a
transmitter and is intentionally **not implemented** — a signal-triggered
multi-frequency responder is a jammer regardless of UI framing, and operating
one is unlawful in most jurisdictions.

The lawful research equivalent that *is* implemented:

- **Multiple-frequency testing** — the Frequency List stores candidates and lets
  you step through them manually; selecting one re-parks the **receiver** on it.
- **Controlled measurement** — point the analyzer at a signal produced by your
  own, properly licensed test transmitter or signal generator (see
  `LEGAL_TEST_SETUP.md`) and observe how the tool measures and decodes it.

## 4. Radio resource lifecycle

Every code path that touches the radio pairs acquisition with release:

- Scanner: `begin → reset → idle → load_preset → (sweep) → idle → sleep → end`.
- Capture: `begin → reset → idle → load_preset → set_frequency → start_async_rx →
  (analyze) → stop_async_rx → idle → sleep → end`.
- Scene `on_exit` handlers stop the active engine, and `rf_analyzer_app_free`
  stops both engines before freeing them, so exiting the app always leaves the
  CC1101 in sleep.

## UI structure (`scenes/`)

A standard Flipper **ViewDispatcher + SceneManager** app. One scene per screen
(`start`, `scan`, `signals`, `analyze`, `freq_list`, `settings`, `about`),
sharing four views: a `Submenu`, a `VariableItemList` (settings), a `Widget`
(analyze/about) and one custom `View` for the live scan display. The scene table
in `scenes/rf_analyzer_scene_config.h` generates the enum and handler arrays via
X-macros, so adding a screen is a one-line change plus a handler file.
