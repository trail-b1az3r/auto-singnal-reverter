# RF Analyzer — Flipper Zero Sub-GHz research tool

A native Flipper Zero application for **authorized RF research**: it discovers,
measures, captures and decodes Sub-GHz signals using the Flipper's built-in
CC1101 radio and antenna. An optional **Auto Inverse Test** mode can transmit
a logical inverse of a captured signal for authorized laboratory equipment testing.

> **Receive-first design.** The primary operation is receive-only. The Auto
> Inverse Test mode is **disabled by default** and requires explicit user
> configuration. All TX paths are gated by: single test frequency, maximum TX
> duration, enforced cooldown, decode requirement, firmware frequency checks,
> and a physical emergency stop (Back button). It is not a jammer — it
> transmits a bounded, time-limited inverse waveform for receiver testing.

## Features

| UI screen            | What it does                                                                 |
|----------------------|-----------------------------------------------------------------------------|
| **Scan**             | RSSI sweep across a configurable range; live frequency + RSSI + RX state    |
| **Detected Signals** | Session list of candidates: frequency, peak RSSI, decode status             |
| **Analyze**          | Park on one frequency, run timing analysis, show protocol or "Unknown"      |
| **Frequency List**   | Save/import candidate frequencies; step through them manually               |
| **Auto Inverse Test**| **NEW:** Monitor test frequency, detect signal, generate & transmit inverse  |
| **Settings**         | Band, step size, dwell, RSSI trigger, modulation preset + Auto Test config  |
| **About**            | Scope, safety note and firmware target                                       |
| **TX/RX status**     | Prominent indicator — shows RX/IDLE normally, `>>> AUTO TX <<<` when TX     |

Each detected signal records:

```
Frequency: 433.920 MHz
Signal:    Detected
Strength:  -52 dBm
Modulation: OOK/ASK or 2-FSK (from the active demod preset)
Duration:  128 ms
Status:    Analyzed / Unknown
```

### Auto Inverse Test (optional, disabled by default)

When enabled in Settings, the **Auto Inverse Test** screen implements:

```
RX → ANALYZE → INVERT → TX → COOLDOWN → RX
```

...

### Remove All Restrictions (advanced, authorized lab only)

In Settings, there is an additional option **Remove All Restrictions**. When
enabled:

- All safeguards are bypassed: no frequency limits, no TX duration limit,
  no cooldown, no decode requirement, and firmware frequency validation is
  skipped.
- The test can use arbitrary frequencies and transmit continuously.
- The `>>> AUTO TX <<<` indicator still appears during transmission.
- **This mode is for authorized laboratory use only** — it must be used in a
  shielded enclosure with equipment you own or are explicitly authorized to test.
- To re-enable safeguards, disable this option and restart the Auto Inverse Test.

**Safeguards table** (see `docs/LEGAL_TEST_SETUP.md` for the full table with
and without this mode enabled).

1. **RX MONITOR** — Park on configured test frequency, capture edges
2. **DETECTED** — Signal above RSSI threshold found
3. **ANALYZING** — Measure pulse timing, estimate bitrate
4. **GENERATING** — Build logical inverse (levels flipped, timing preserved)
5. **TRANSMITTING** — Send inverse waveform (bounded by TX Duration)
6. **COOLDOWN** — Enforce minimum gap before next cycle
7. **Back to RX** — Resume monitoring

**Safeguards:**
- Disabled by default (`Auto Inverse` = OFF)
- Single user-configured test frequency (not a range)
- Max TX duration: 1 ms – 10 s (hard-coded limit)
- Cooldown: 0 – 60 s (0 only if "Remove Cooldown Limit" = YES)
- Only transmits if signal decoded/supported (`Require Decode` = YES)
- Emergency stop: Back button immediately halts TX and releases radio
- `>>> AUTO TX <<<` displayed prominently during transmission
- App exit stops all TX and releases radio
- Firmware frequency/power restrictions always respected
- NRF24 mode setting available; on-air NRF24 TX is a documented stub (no NRF24
  HAL in the official FAP SDK — needs an external module + bundled GPIO driver)

## Firmware / API target

- **Official Flipper Zero firmware SDK** (the `flipperzero-firmware` release
  line). Developed against the stable API set exposed by:
  - `lib/subghz/devices` (`subghz_devices_*`) — the supported external-app
    radio API: bring-up, tuning, RX, RSSI, async RX, and async TX on CC1101.
  - NRF24: settings plumbing only — the official FAP SDK publishes no NRF24
    HAL, so `rf_nrf24_transmit_inverse()` is a stub returning
    `RfInvertErrUnsupportedModulation` until an external-module GPIO driver is
    bundled (the approach used by catalog NRF24 apps).
  - Raw-timing analysis is done in-app from the async RX edge stream. Full
    firmware protocol decoding is intentionally not used: its decoder registry
    is not part of the public FAP SDK.
  - `gui` scene manager / view dispatcher, `notification`.
- Built as an **external FAP** with `ufbt`.
- These APIs are also present in the common community distributions
  (Unleashed / RogueMaster / Momentum). If you build against one of those,
  confirm the `subghz_devices_*` and preset enum names match your SDK.

## Build & install

Using **ufbt** (recommended for a single external app):

```bash
# 1. Install ufbt (once)
python3 -m pip install --upgrade ufbt

# 2. From this directory (the one containing application.fam)
cd rf_analyzer
ufbt            # builds dist/rf_analyzer.fap

# 3. With the Flipper connected over USB, build + install + launch:
ufbt launch
```

Alternatively, drop the tree into `applications_user/rf_analyzer/` inside a full
firmware checkout and build with `./fbt fap_rf_analyzer`, or copy the built
`.fap` to `/ext/apps/Sub-GHz/` on the SD card and launch it from the Flipper
menu.

### Requirements

- A Flipper Zero on current stable firmware (or a compatible community build).
- `ufbt` (pulls the matching SDK automatically), or a full firmware source tree.
- Optional: `/ext/subghz/assets/keeloq_mfcodes` on the SD card improves naming
  of some rolling-code protocols. Its absence is handled gracefully.

## Repository layout

```
rf_analyzer/
├── application.fam                  # app manifest / build config
├── rf_analyzer.c                    # entry point, wiring, session state
├── rf_analyzer_i.h                  # app struct + shared prototypes
├── helpers/
│   ├── rf_analyzer_types.h          # data structures, presets, bands
│   ├── rf_analyzer_scanner.[ch]     # RSSI-sweep worker (RX)
│   ├── rf_analyzer_capture.[ch]     # fixed-frequency capture + timing (RX)
│   ├── rf_analyzer_tx.[ch]          # TX engine + inverse generation
│   └── rf_analyzer_nrf24.[ch]       # NRF24L01+ support for Auto Test
├── views/
│   └── rf_analyzer_scan_view.[ch]   # custom live-scan view
├── scenes/                          # scene manager + one file per screen
│   ├── rf_analyzer_scene_auto_test.c   # Auto Inverse Test scene
│   └── ...
├── images/                          # 10x10 app icon
└── docs/
    ├── ARCHITECTURE.md              # RF scan/analysis architecture
    └── LEGAL_TEST_SETUP.md          # example lawful bench setup (incl. Auto Test)
```

## Safety & legal scope

This tool is for signal **discovery, visualization, measurement and decoding**
of equipment you own or are explicitly authorized to test, on frequencies you
are permitted to receive, in accordance with the radio regulations of your
jurisdiction.

The **Auto Inverse Test** mode is for **authorized laboratory use only**. It
transmits a bounded, time-limited logical inverse of a detected signal to
measure receiver rejection/immunity on your own equipment, inside a shielded
enclosure or conducted test setup. It is not a jammer and must not be used to
interfere with third-party RF systems. Operating a jammer or transmitting
without authorization is illegal in most countries.

See `docs/ARCHITECTURE.md` for how scanning and decoding work, and
`docs/LEGAL_TEST_SETUP.md` for a legal bench configuration using a
user-controlled test transmitter (including Auto Inverse Test setup).
