// ---- Versioning ------------------------------------------------------------
// Always update this field with latest patch updates
#ifndef EVD_FW_VERSION
// 1.0.0.1: dashboard-only - Stream tab shows players with their room or
//           "Discovered (muted)" and the room list as a table.
#define EVD_FW_VERSION "1.0.0.1"
// EVDCTR: capture side of the split Enkelvoud server
#endif
#define FW_VERSION EVD_FW_VERSION

/*
  ============================================================================
  EVDCTR (ESP32-S3) - Enkelvoud split server, CONTROLLER half
  I2S / USB / NETWORK / DLNA audio -> pre-processing -> Opus encode
                                    -> SPI link -> EVDSTR
  ============================================================================

  EVDCTR is the old EnkelvoudServer with its network *audio* role removed.
  It captures, pre-processes and encodes; it does not know what a player is,
  never opens AUDIO_UDP_PORT, and never speaks the player protocol. Everything
  that reaches a player crosses the pin link in EVDLinkSpi.h as one Opus frame
  per 20 ms, and EVDSTR puts it on the wire. EVDDownlink.h is the *control*
  plane - an HTTP relay to STR - not the audio path.

  ============================ Core policy ==================================

  This is the whole point of the split, so it is enforced structurally, not by
  priority tuning:

  CORE 0 - everything that is not opus_encode():
    WiFi / lwIP            system tasks
    AsyncTCP + HTTP        the dashboard, /api/state, /api/stats, OTA upload
    audioRead    pri 4     I2S DMA drain, USB read, or network yield
    audioProc    pri 2     rate estimate, two-stage resample, quiet, volume
    dlnaSsdp     pri 2     SSDP announce/respond
    dlnaFetch    pri 2     HTTP WAV pull for DLNA
    encProbe     pri 1     counters-only: proves the encode chain with no STR
    loop()       pri 1     factory reset, pot, discovery, NVS flush, OTA
    (no netLink task exists: outbound control is a synchronous HTTPClient
     call from the route handler, bounded at 1.5 s)

  CORE 1 - opus_encode() ONLY:
    audioEncode  pri 5     xQueueReceive(encodeQueue) -> opus_encode()
                           -> linkSendFrame()

  audioEncode does nothing else. It does not build JSON, does not log at
  frame rate, does not touch WiFi, does not read NVS. Its input is a
  pre-processed PCM frame; its output is a payload handed to the SPI link.
  Every encoder control (bitrate, complexity, VBR, application, sample rate)
  is applied here because libopus requires it to be serialised with
  opus_encode(), but each one is a single ctl call, not work.

  ============================ What moved, and why ==========================

  - udpSendTask (the Opus -> subscribers pacer) is GONE. EVDSTR owns it.
  - The subscriber table, playerNodes, zones, node perf and every
    player-facing route (/api/player/x, /api/node_stats, /api/zones/x,
    /api/config, /api/source, /api/cmd) are GONE from this board.
  - OTA no longer pushes to nodes or to a source board over UART; that was a
    network job. CTR updates itself, and a new STR image is uploaded to CTR
    and pushed to STR over WiFi as an HTTP multipart POST (EVDOTA.h) - not
    over the pin link, which carries audio only.
  - The audio pacer (EVDPacer.h) is GONE: it exists to keep an absolute send
    schedule, and the send schedule now lives on STR.

  ============================ Project files ================================

  --- Link (shared with EVDSTR, byte-identical copies) ----------------------

  EVDLink.h
      The wire contract: magic, version, the 8-byte frame header, flag bits,
      and the header validator. No Arduino, no opus, no globals — the same
      file is committed to both repos so the two ends cannot drift.

  --- Inline headers (single translation unit) ------------------------------

  EVDState.h
      Every mutable global on this board: input source enum, encoder
      settings, queues, statistics counters, mutexes, log ring, the SPI link
      counters mirrored for the dashboard.
  EVDHelpers.h
      PrefsGuard, lockFor/unlockFor, JSON/HTML escaping, request-body
      accumulation.
  EVDWiFi.h
      STA bring-up with AP fallback, mDNS, discovery responder, network test,
      WiFi scan endpoints, per-mode encoder presets in NVS.
  EVDLinkSpi.h
      SPI2 master half of the link. Owns SCLK/MOSI/IRQ out, CS in.
  EVDDownlink.h
      The CTR-side face of the link: the HTTP relay that forwards operator
      requests to STR (zones, player list, config pushes) and caches the
      replies for the dashboard. There is no linkQueue - the pin link is
      audio only, and the control plane is HTTP. See the file header.
  EVDAudio.h
      Resampler, quiet filters, anti-alias, setup_opus, audioReadTask,
      audioProcessingTask, audioEncodeTask (core 1, encode + link send only).
  EVDDLNA.h
      DLNA MediaRenderer: SSDP, description/SCPD, SOAP. Fetch task runs on
      core 0 here.
  EVDUsb.h
      USB Audio Class input (unchanged role).
  EVDRoutes.h
      Route table: dashboard, /api/state, /api/stats, /api/log/x, network,
      OTA, DLNA, encoding controls, and the /api/link/x control plane that
      proxies to STR.
  EVDOTA.h
      CTR self-update (multipart upload + /api/ota/apply), and the push of a
      new EVDSTR image to STR over WiFi. Spooled to LittleFS, not the link.
  EVDUart.h
      The source board's UART: RX line assembly, source requests, BT list and
      OTA forwarding to the ESP32 source board.
  EVDMain.h
      setup() / loop().

  --- Generated / editable dashboard ---------------------------------------

  EVDCTRL_GZ.h
      gzip of EVDCTRL.h, generated by gen_gz.py. STALE-FILE HAZARD: edit
      EVDCTRL.h, then run gen_gz.py, or the old page is served silently.
  EVDCTRL.h
      The dashboard, as a PROGMEM string.
  EVDNTW.h
      Standalone network-setup page served at /settings during AP fallback.

  --- Non-C++ --------------------------------------------------------------

  build_opt.h        build flags (no AsyncTCP pinning change needed: HTTP is
                     core 0 here, which is already the default)
  partitions.csv     2 x 4 MB OTA app + LittleFS + NVS + coredump
  gen_gz.py / .bat   dashboard gzip generator
  ============================================================================
*/

#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include "opus.h"
#include "EVDLink.h"
// The dashboard is a PROGMEM gzip blob generated from EVDCTRL.h by gen_gz.py.
// It must be visible to EVDRoutes.h, which serves it.
#include "EVDCTRL_GZ.h"

// ---- Pin assignments -------------------------------------------------------
static const int pinBtLrc     = 15;
static const int pinBtDin     = 16;
static const int pinBtBclk    = 17;

static const int pinDacLrc    = 4;
static const int pinDacDout   = 5;
static const int pinDacBclk   = 6;

static const int pinVolumePot = 7;

static const int pinSrcUartTx = 9;
static const int pinSrcUartRx = 10;

// ---- Defaults --------------------------------------------------------------
const char *DEFAULT_WIFI_STA_SSID     = "";
const char *DEFAULT_WIFI_STA_PASSWORD = "";
const char *default_ap_ssid           = "Enkelvoud CTR";
const char *default_ap_password       = "";

#define WIFI_CONNECT_TIMEOUT_MS 3000
#define WIFI_TEST_TIMEOUT_MS   15000

// ---- Factory reset ---------------------------------------------------------
#define PIN_FACTORY_RESET     21
#define FACTORY_RESET_HOLD_MS 3000

// ---- Audio format ----------------------------------------------------------
#define SOURCE_SAMPLE_RATE      48000
#define OPUS_SAMPLE_RATE_48K    48000
#define OPUS_SAMPLE_RATE_24K    24000
#define OPUS_SAMPLE_RATE_DEFAULT OPUS_SAMPLE_RATE_48K
#define OPUS_FRAME_MS           20
#define CHANNELS                2
#define OPUS_BITRATE            96000
// Must equal LINK_MAX_PAYLOAD and EVDSTR's MAX_OPUS_PACKET_LEN: a frame
// larger than this is dropped on the link, which sounds exactly like the
// "click on every loud transient" the old server documented at >150 kbps.
#define OPUS_MAX_PACKET_BYTES 1024
static_assert(OPUS_MAX_PACKET_BYTES == LINK_MAX_PAYLOAD,
              "OPUS_MAX_PACKET_BYTES must equal LINK_MAX_PAYLOAD");

// ---- Buffers ---------------------------------------------------------------
#define I2S_PORT_IN           I2S_NUM_0
#define I2S_PORT_OUT          I2S_NUM_1
#define I2S_READ_CHUNK_BYTES  1024

// 16 slots = 320 ms. The encoder drains at exactly 50 fps.
#define RAW_QUEUE_LEN   16
// 8 slots. On the unsplit server this fed a pacer on the same chip; here the
// consumer is a 1 ms SPI transfer plus opus_encode, so 160 ms of slack is
// generous and the freed 8 KB of DRAM goes to the HTTP stack.
#define OPUS_QUEUE_LEN  8
// 4 slots (80 ms) between audioProc (core 0) and audioEncode (core 1).
#define ENCODE_QUEUE_LEN 4

// ---- UDP transport (control plane only; audio belongs to EVDSTR) -----------
#define CTRL_UDP_PORT       4203
#define CLOCK_SYNC_UDP_PORT 4204
#define DISCOVERY_PORT      4201
#define DISCOVERY_MAGIC_REQ "ENKELVOUDP/1 DISCOVER"
#define DISCOVERY_MAGIC_RPY "ENKELVOUDP/1 SERVER"
// How often an unpaired board broadcasts a probe. 3 s is fast enough that two
// freshly flashed boards pair while you are still looking at the dashboard, and
// slow enough that a lone board does not chatter on a busy LAN.
#define DISCOVERY_TX_MS     3000
// How often a board re-announces itself once paired, so a partner that was
// rebooted with a stale address can find it again.
#define DISCOVERY_ANNOUNCE_MS 60000

#define PENDING_MAX_FRAMES 4096

// ---- USB Audio Class -------------------------------------------------------
// EVDUsb.h targets EspUsbDevice 3.x.
//
// The #include of the library lives HERE, at the top of the sketch, and nowhere
// else. arduino-cli decides which libraries to compile - and whose include
// paths to add - by scanning the .ino/.pde files for #include lines only. A
// library included from a .h file alone is never resolved, so
// <EspUsbDevice.h> stayed off the include path, EVDUsb.h's probe said "not
// found", and every USB function compiled to its no-op fallback: a build with
// no error, no USB input, and a #pragma message easy to scroll past.
//
// The #include is unconditional on purpose: arduino-cli must see the literal
// line to resolve the library, and __has_include cannot be relied on here
// because the probe runs BEFORE the library's include path has been added.
// A missing library is a build error with the library's name in it, which is
// the honest outcome - flashing a board that silently cannot do USB is not.
#include <EspUsbDevice.h>
#define EVD_HAS_USB_DEVICE_LIB 1
// Readable as a value at runtime, so a handler can refuse without #if inside a
// function body.
#define EVD_USB_INPUT_BUILT (EVD_HAS_USB_DEVICE_LIB != 0)
#define EVD_USB_VID 0x303a
#define EVD_USB_PID 0x40e1     // CTR PID; STR uses 0x40e2 so Windows keeps two

// ---- Logging ---------------------------------------------------------------
#define LOG_BAUD 115200
extern uint8_t gSerialLogLevel;
inline void evdLogPrintf(char level, const char *fmt, ...);
#define LOGI(fmt, ...) do { if (gSerialLogLevel >= 3) evdLogPrintf('I', fmt, ##__VA_ARGS__); } while (0)
#define LOGW(fmt, ...) do { if (gSerialLogLevel >= 2) evdLogPrintf('W', fmt, ##__VA_ARGS__); } while (0)
#define LOGE(fmt, ...) do { if (gSerialLogLevel >= 1) evdLogPrintf('E', fmt, ##__VA_ARGS__); } while (0)

// ---- Misc ------------------------------------------------------------------
static const unsigned long NVM_SAVE_DELAY_MS = 5000;

// ---- Shared POD structs ----------------------------------------------------
struct RawChunk {
  uint8_t  data[I2S_READ_CHUNK_BYTES];
  uint32_t len;
  uint32_t captured_ms;
};

// One encoded frame on its way from audioEncode (core 1) to the link.
// Carries only what STR needs to build its wire header; there is no captured
// timestamp because STR stamps its own.
struct OpusPacket {
  uint8_t  data[OPUS_MAX_PACKET_BYTES];
  int      len;
  uint16_t samples;
};

struct EncodeFrame {
  int16_t  pcm[OPUS_SAMPLE_RATE_48K / (1000 / OPUS_FRAME_MS) * CHANNELS];
  uint32_t captured_ms;
  uint32_t seq;
  uint16_t samples;
};

// ---- Forward declarations --------------------------------------------------
// Zones live on EVDSTR (EVDPlayers.h there). This board only relays zone
// requests, so it has no zone table and no zone persistence at all.
inline void configureQuietFilters();
inline void applyQuietFilter(int16_t *buf, uint32_t frames);
inline void setup_opus(uint32_t sampleRate);
inline void setup_queues_and_tasks();
// Defined further down the include chain; declared here because headers
// earlier in the chain call them from comments/handlers.
inline void serviceDiscovery();
inline void serviceReceiverLink();
inline void serviceUsbAudio();
inline void pollUsbAudioEvents();
inline void serviceVolumePot();
inline void saveWiFiSettings();
inline void loadWiFiSettings();
// saveDownlinkSettings()/loadDownlinkSettings() are defined in EVDDownlink.h.
inline void refreshDownlinkCache();
inline void initDlna();
inline bool setupLinkSpi();
inline bool setup_i2s_in();

// ============================================================================
//  Headers
// ============================================================================
#include "EVDState.h"
#include "EVDHelpers.h"
#include "EVDWiFi.h"
#include "EVDLinkSpi.h"
#include "EVDDownlink.h"
#include "EVDAudio.h"
#include "EVDDLNA.h"
#include "EVDUart.h"    // the source board's UART grammar
#include "EVDUsb.h"
#include "EVDOTA.h"     // route handlers EVDRoutes.h registers
#include "EVDNTW.h"     // the /settings page served during AP fallback
#include "EVDRoutes.h"
#include "EVDMain.h"
