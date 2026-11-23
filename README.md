# EVDCTR - Enkelvoud split server, CONTROLLER half

ESP32-S3. Captures audio, pre-processes it, Opus-encodes it, and hands one
frame per 20 ms to **EVDSTR** over a 4-wire SPI link. It has no idea what a
player is: no subscriber table, no audio UDP socket, no zone code.

```
I2S / USB / DLNA ──► audioRead (c0) ──► audioProc (c0) ──► audioEncode (c1)
                                                                │
                                                          opus_encode()
                                                                │
                                                          pin link (SPI)
                                                                ▼
                                                             EVDSTR
```

## Core policy

| Core | Tasks | Rule |
|---|---|---|
| **0** | WiFi/lwIP, AsyncTCP+HTTP, `audioRead` pri 4, `audioProc` pri 2, `dlnaSsdp` pri 2, `dlnaFetch` pri 2, `loop()` pri 1 | everything that is not the encoder |
| **1** | `audioEncode` pri 5 | **`opus_encode()` and the link send, nothing else** |

`audioEncodeTask` (EVDAudio.h) is the only thing pinned to core 1. Its loop is:
counter rollover → pending encoder re-create → receive one pre-processed frame →
apply pending ctl settings → `opus_encode()` → `linkSendFrame()`. No JSON, no
`String`, no WiFi, no NVS, no per-frame logging. The DLNA fetch task moved off
core 1 to make that literally true.

## Pin link (to EVDSTR)

| CTR | direction | STR | signal |
|---|---|---|---|
| GPIO11 | → | GPIO11 | SCLK |
| GPIO12 | → | GPIO12 | MOSI |
| GPIO13 | → | GPIO13 | IRQ (frame ready) |
| GPIO14 | ← | GPIO14 | CS (slave ready, active low) |
| GND | ↔ | GND | **required**, one per ~10 cm of wire |

8 MHz, mode 0, one transaction per frame: 8-byte header + up to 1024 bytes of
Opus payload ≈ 1 ms inside a 20 ms period. The header carries the sample count
and encoder rate per frame, so a 24 kHz/48 kHz re-create on the dashboard
reaches EVDSTR with no control channel. `EVDLink.h` is byte-identical in both
repos and is the single source of truth for the format.

A missing/stuck EVDSTR costs one frame, bounded by `LINK_CS_TIMEOUT_US` +
`LINK_XFER_TIMEOUT_US` (10 ms) — the encode core is never blocked longer.

## Endpoints

CTR keeps the operator's dashboard at the same URL. Routes that only make
sense on the stream side are **relayed** to EVDSTR over HTTP (`EVDDownlink.h`):

| Route | Where it runs |
|---|---|
| `/`, `/api/state`, `/api/stats`, `/api/log/*`, `/api/network*`, `/api/restart` | CTR |
| `/api/audio/encoding*`, `/api/source`, `/api/cmd`, `/api/bt/*` | CTR (encoder + source board are here) |
| `/api/config`, `/api/zones/*`, `/api/link/players`, `/api/link/zones` | relayed to EVDSTR |
| `/api/link/pair`, `/api/link/state` | CTR (pair the STR address) |
| `/api/ota/*` | CTR self-update, and push an image to EVDSTR |

## Files

| File | What it is |
|---|---|
| `EVDLink.h` | wire contract (shared with EVDSTR, identical copy) |
| `EVDCTR.ino` | constants, POD structs, include order, architecture notes |
| `EVDState.h` | every mutable global |
| `EVDHelpers.h` | locks, escaping, request-body accumulation, log ring |
| `EVDWiFi.h` | STA + AP fallback, mDNS, discovery, scan, mode presets |
| `EVDLinkSpi.h` | SPI2 **master** half of the link |
| `EVDDownlink.h` | HTTP relay to EVDSTR + cached stream state |
| `EVDAudio.h` | resampler, quiet filters, the three audio tasks |
| `EVDDLNA.h` | UPnP MediaRenderer, SSDP, SOAP, fetch task (core 0) |
| `EVDUart.h` | source-board UART grammar (BT/AUX/USB) |
| `EVDUsb.h` | UAC1 capture |
| `EVDRoutes.h` | route table |
| `EVDOTA.h` | self-OTA + EVDSTR image push |
| `EVDMain.h` | `setup()` / `loop()` |
| `EVDCTRL.h` | dashboard source; `EVDCTRL_GZ.h` is generated from it |

## Build

Arduino IDE / arduino-cli, board **ESP32S3 Dev Module**, 16 MB flash,
8 MB PSRAM (OPI), partition scheme *Custom* with `partitions.csv`, plus the
libraries `ESPAsyncWebServer`, `AsyncTCP`, `ArduinoJson`, `opus` (libopus for
ESP32), `EspUsbDevice`.

After editing `EVDCTRL.h` you **must** run `python gen_gz.py`, or the old
dashboard is served with no warning.

## First boot

1. Flash. With no credentials the board comes up as AP `EVDCTR` at
   192.168.4.1 and serves `/settings`.
2. Join a network, set the board name. It reboots and answers on
   `http://<name>.local/`.
3. On the **Stream** tab, enter EVDSTR's IP and press Pair.
4. The **Status** tab shows the pin-link health: `link ok` means EVDSTR is
   clocking frames; `link DOWN` with a rising CS-timeout count means the
   wiring, the ground, or EVDSTR's firmware.
