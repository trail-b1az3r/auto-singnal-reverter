# RF scanning & analysis architecture

This document explains the signal path and how the application is structured.
The primary design goal is: **discover activity quickly, then let the user
inspect one signal at a time in depth.** An optional **Auto Inverse Test** mode
adds a controlled, gated transmit path for authorized laboratory equipment testing.

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
   ├──────────────► Auto Test TX (subghz_devices async TX / furi_hal_nrf24)
   │                    bounded inverse waveform transmission
   │
   └──────────────► UI timer (FuriTimer)  ── pulls live status @ 4–10 Hz,
                         updates view models (lock-protected)
```

The GUI thread never does RF work directly, so the interface stays responsive
while a sweep, decode, or test transmission is running. The scanner, capture,
and TX engines are mutually exclusive — each acquires the radio (CC1101 or
NRF24) while its scene is active and releases it on scene exit.

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

Captured data lives **only in memory** for the session.

## 3. Auto Inverse Test — controlled inverse waveform transmission (`helpers/rf_analyzer_tx.c`, `scenes/rf_analyzer_scene_auto_test.c`)

**Disabled by default.** Only active when user explicitly enables `Auto Inverse`
in Settings and opens the *Auto Inverse Test* scene.

### Workflow (state machine)

```
RX MONITOR → DETECTED → ANALYZING → GENERATING → TRANSMITTING → COOLDOWN → RX MONITOR
```

1. **RX MONITOR** — Park on configured `test_frequency`, run capture to stream edges.
2. **DETECTED** — Edge count exceeds threshold (signal present above RSSI threshold).
3. **ANALYZING** — Measure pulse timing, estimate bitrate, verify modulation support.
4. **GENERATING** — Build logical inverse waveform:
   - Modulation classified from RX preset (OOK/2-FSK/NRF24)
   - Edge sequence inverted: levels flipped (mark↔space), durations preserved
   - *Limitation:* Full edge history not captured; inverse synthesized from
     measured avg/min pulse width and estimated burst duration.
5. **TRANSMITTING** — Send inverse via:
   - **Sub-GHz:** `subghz_devices_start_async_tx()` with edge callback (CC1101)
   - **NRF24:** `furi_hal_nrf24_tx()` packetized (NRF24L01+)
   - Duration clamped to `tx_duration_ms` (max 10 s)
   - Prominent `>>> AUTO TX <<<` displayed on screen
6. **COOLDOWN** — Enforce `cooldown_ms` minimum gap (0–60 s, 0 only if override enabled)
7. **Back to RX** — Resume monitoring on test frequency

### Safeguards (enforced in code)

| Safeguard | Implementation |
|-----------|----------------|
| Disabled by default | `auto_test_config.enabled = false` at startup |
| Explicit enable | User must toggle `Auto Inverse` = ON in Settings |
| Single test frequency | `test_frequency` setting (predefined list, not a range) |
| Max TX duration | `tx_duration_ms` clamped to `RF_AUTO_TEST_MAX_DURATION_MS` (10 s) |
| Cooldown period | `cooldown_ms` enforced; 0 only if `remove_cooldown_limit = true` |
| Decode requirement | `require_decode = true` by default; unsupported modulations rejected |
| Emergency stop | Back button → `rf_tx_emergency_stop()` / `rf_nrf24_emergency_stop()` |
| AUTO TX indicator | Widget shows `>>> AUTO TX <<<` prominently during TX |
| Exit cleanup | `rf_analyzer_app_free()` calls `rf_tx_engine_free()` + `rf_nrf24_deinit()` |
| Firmware restrictions | `subghz_devices_is_frequency_valid()` checked before every TX |
| NRF24 mode | Separate radio path, same safeguards, packetized transmission |

### TX waveform generation (`rf_tx_generate_inverse`)

- Input: `RfCaptureStats` (edge count, min/avg/max pulse, est. bitrate) + `RfSignal` (freq, duration, preset)
- Output: `RfTxWaveform` with `edges[]` array (level, duration_us)
- Modulation: OOK → `FuriHalSubGhzPresetOok650Async`; 2-FSK → `FuriHalSubGhzPreset2FSKDev476Async`
- NRF24 handled separately via `rf_nrf24_transmit_inverse()`

## 4. Radio resource lifecycle

Every code path that touches the radio pairs acquisition with release:

- Scanner: `begin → reset → idle → load_preset → (sweep) → idle → sleep → end`.
- Capture: `begin → reset → idle → load_preset → set_frequency → start_async_rx →
  (analyze) → stop_async_rx → idle → sleep → end`.
- Auto Test TX (Sub-GHz): `begin → reset → idle → load_preset → set_frequency →
  start_async_tx → (transmit) → stop_async_tx → idle → sleep → end`.
- Auto Test TX (NRF24): `furi_hal_nrf24_init() → set_channel/address/rate/power →
  set_mode(TX) → (transmit packets) → set_mode(PowerDown) → deinit()`.
- Scene `on_exit` handlers stop the active engine, and `rf_analyzer_app_free`
  stops all engines before freeing them, so exiting the app always leaves the
  radios in sleep/power-down.

## UI structure (`scenes/`)

A standard Flipper **ViewDispatcher + SceneManager** app. One scene per screen
(`start`, `scan`, `signals`, `analyze`, `freq_list`, `settings`, `about`,
`auto_test`), sharing four views: a `Submenu`, a `VariableItemList` (settings),
a `Widget` (analyze/about/auto_test) and one custom `View` for the live scan
display. The scene table in `scenes/rf_analyzer_scene_config.h` generates the
enum and handler arrays via X-macros, so adding a screen is a one-line change
plus a handler file.
