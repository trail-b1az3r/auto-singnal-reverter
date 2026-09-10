# RF Analyzer (RX) — Flipper Zero Sub-GHz research tool

A native Flipper Zero application for **authorized RF research**: it discovers,
measures, captures and decodes Sub-GHz signals using the Flipper's built-in
CC1101 radio and antenna. No external hardware is required.

> **Receive-only by design.** This application never transmits. There is no
> TX code path, no replay, no "inverse/response" generation and no jamming
> anywhere in the source. If you need to emit a test signal, use a separate,
> properly licensed signal generator or a transmitter you own and are
> authorized to operate (see `docs/LEGAL_TEST_SETUP.md`).

## Features

| UI screen         | What it does (receive side only)                                        |
|-------------------|-------------------------------------------------------------------------|
| **Scan**          | RSSI sweep across a configurable range; live frequency + RSSI + RX state |
| **Detected Signals** | Session list of candidates: frequency, peak RSSI, decode status      |
| **Analyze**       | Park on one frequency, run protocol decoders, show protocol or "Unknown" |
| **Frequency List**| Save/import candidate frequencies; step through them manually            |
| **Settings**      | Band, step size, dwell, RSSI trigger, modulation preset                  |
| **About**         | Scope, safety note and firmware target                                   |
| **TX/RX status**  | Prominent indicator — hard-wired to RX/IDLE because there is no TX       |

Each detected signal records:

```
Frequency: 433.920 MHz
Signal:    Detected
Strength:  -52 dBm
Modulation: OOK 650kHz (preset) / decoded protocol name, or Unknown
Duration:  128 ms
Status:    Analyzed / Unknown
```

## Firmware / API target

- **Official Flipper Zero firmware SDK** (the `flipperzero-firmware` release
  line). Developed against the stable API set exposed by:
  - `furi_hal_subghz` — radio bring-up, tuning, RX, RSSI, async RX.
  - `lib/subghz` — `SubGhzWorker`, `SubGhzReceiver`, `SubGhzEnvironment` and the
    protocol registry for decoding.
  - `gui` scene manager / view dispatcher, `notification`.
- Built as an **external FAP** with `ufbt`.
- These APIs are also present in the common community distributions
  (Unleashed / RogueMaster / Momentum). If you build against one of those,
  confirm the `furi_hal_subghz` and `lib/subghz` symbol names match your SDK —
  they occasionally rename presets or the `set_frequency` helper.

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
├── application.fam              # app manifest / build config
├── rf_analyzer.c                # entry point, wiring, session state
├── rf_analyzer_i.h              # app struct + shared prototypes
├── helpers/
│   ├── rf_analyzer_types.h      # data structures, presets, bands
│   ├── rf_analyzer_scanner.[ch] # RSSI-sweep worker (RX)
│   └── rf_analyzer_capture.[ch] # fixed-frequency capture + decode (RX)
├── views/
│   └── rf_analyzer_scan_view.[ch] # custom live-scan view
├── scenes/                      # scene manager + one file per screen
├── images/                      # 10x10 app icon
└── docs/
    ├── ARCHITECTURE.md          # RF scan/analysis architecture
    └── LEGAL_TEST_SETUP.md      # example lawful bench setup
```

## Safety & legal scope

This tool is for signal **discovery, visualization, measurement and decoding**
of equipment you own or are explicitly authorized to test, on frequencies you
are permitted to receive, in accordance with the radio regulations of your
jurisdiction. It intentionally has no capability to transmit, replay, respond
to, or interfere with third-party RF systems. Operating a jammer or transmitting
without authorization is illegal in most countries.

See `docs/ARCHITECTURE.md` for how scanning and decoding work, and
`docs/LEGAL_TEST_SETUP.md` for a legal bench configuration using a
user-controlled test transmitter.
