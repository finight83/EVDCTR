#pragma once
// ============================================================================
//  EVDState.h - EVDCTR mutable globals
// ============================================================================
//
//  Everything here is owned by one board. There is no subscriber table and no
//  player list: the only "remote" state this board keeps is what EVDSTR tells
//  it (see EVDDownlink.h, gDownlinkCache) and the operator can see that on the
//  dashboard's Stream tab.
// ============================================================================
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <atomic>

// ---- Input source ----------------------------------------------------------
enum ServerInputSource : uint8_t {
  SRC_FROM_LOCAL  = 0,   // I2S in from the source board (BT/AUX)
  SRC_FROM_USB    = 1,   // UAC1 host audio
  SRC_FROM_NETWORK= 2,   // DLNA HTTP WAV pull
};
volatile uint8_t serverInputSource = SRC_FROM_LOCAL;

// ---- Encoder ---------------------------------------------------------------
volatile uint32_t g_opusSampleRate        = OPUS_SAMPLE_RATE_DEFAULT;
volatile uint32_t g_pendingOpusSampleRate = 0;   // 0 = none, 0xFFFFFFFF = force
volatile uint16_t g_opusFrameSamples      = OPUS_SAMPLE_RATE_DEFAULT / (1000 / OPUS_FRAME_MS);
volatile uint32_t g_sourceSampleRate      = SOURCE_SAMPLE_RATE;
volatile bool     g_rateChangeFlushPending = false;
volatile bool     g_rateEstimatorNeedsReset = false;
volatile bool     g_encoderSettingsDirty   = false;

volatile int g_newBitrate        = OPUS_BITRATE;
volatile int g_currentBitrate    = OPUS_BITRATE;
volatile int g_pendingComplexity = -1;
volatile int currentOpusComplexity = 10;
volatile int g_newVbr            = 1;
volatile int g_currentVbr        = 1;
volatile int g_newVbrConstraint  = 1;
volatile int g_currentVbrConstraint = 1;
volatile int g_newSignal         = OPUS_AUTO;
volatile int g_currentSignal     = OPUS_AUTO;
volatile int g_newApplication    = OPUS_APPLICATION_AUDIO;
volatile int g_currentApplication= OPUS_APPLICATION_AUDIO;

// ---- Operator controls -----------------------------------------------------
volatile uint8_t masterVolume = 100;
volatile bool    masterMuted  = false;
volatile uint8_t potVolume    = 100;
volatile bool    quietModeEnabled = false;
volatile bool    g_quietFiltersNeedReconfig = false;
volatile uint16_t audioBufferMs = 200;   // forwarded to STR for the nodes
volatile bool    otaPauseAudio  = false;

// ---- Playback mode ---------------------------------------------------------
enum ModeIdx : uint8_t { MODE_OFF = 0, MODE_MUSIC = 1, MODE_MOVIE = 2 };
volatile uint8_t playbackModeIdx = MODE_MUSIC;
inline const char *playbackModeCStr() {
  switch (playbackModeIdx) {
    case MODE_OFF:   return "off";
    case MODE_MOVIE: return "movie";
    default:         return "music";
  }
}

// ---- Audio sequence (local only; STR owns the wire sequence) ---------------
volatile uint32_t gAudioFrameSeq = 1;

// ---- Queues ----------------------------------------------------------------
QueueHandle_t rawQueue    = nullptr;
QueueHandle_t encodeQueue = nullptr;
QueueHandle_t opusQueue   = nullptr;   // encoded frames waiting for the link

// ---- Mutexes ---------------------------------------------------------------
SemaphoreHandle_t prefsMutex     = nullptr;
SemaphoreHandle_t uartMutex      = nullptr;
SemaphoreHandle_t linkCtrlMutex  = nullptr;
SemaphoreHandle_t logTailMutex   = nullptr;
SemaphoreHandle_t gUsbLifecycleMutex = nullptr;

// ---- Statistics ------------------------------------------------------------
// Encoder-side counters (written by core 1, read by the HTTP task).
volatile uint32_t statOpusEncoded            = 0;
volatile uint32_t statOpusDropped            = 0;
volatile uint32_t statRawDropped             = 0;
volatile uint32_t statEncodeOverrunsTotal    = 0;
volatile uint32_t statEncodeOverrunsLastSec  = 0;
volatile uint32_t statEncodePeakUsLastSec    = 0;
volatile uint32_t statPendingBufDroppedTotal = 0;
volatile uint32_t gAvgEncodeUs               = 0;

uint32_t statEncodeOverrunsThisSec = 0;
uint32_t statEncodePeakUsThisSec   = 0;
uint32_t statPendingBufDroppedThisSec = 0;
uint32_t statPendingBufPeakDepth      = 0;
uint32_t statPendingBufPeakDepthLastSec = 0;
uint32_t statDriftPpm = 0;
uint64_t gRateFramesLostTotal = 0;

portMUX_TYPE gStatsMux = portMUX_INITIALIZER_UNLOCKED;

// ---- Link statistics (mirrored for /api/state and /api/stats) --------------
volatile uint32_t statLinkSent        = 0;
volatile uint32_t statLinkDropped     = 0;
volatile uint32_t statLinkCsTimeouts  = 0;
volatile uint32_t statLinkSilence     = 0;
volatile uint32_t statLinkLastFrameUs = 0;   // wire time of the last good frame
volatile bool     gLinkHealthy        = false;
volatile uint32_t gLinkLastOkMs       = 0;

// ---- Log ring --------------------------------------------------------------
#define EVD_LOG_RING_LEN 32
struct LogRingEntry {
  uint32_t seq;
  uint32_t ms;
  char     level;
  char     text[192];
};
LogRingEntry      gLogRing[EVD_LOG_RING_LEN];
uint32_t          gLogRingSeq = 0;
portMUX_TYPE      gLogRingMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t  gSerialLogLevel = 3;

// ---- Identity / settings ---------------------------------------------------
String serverName      = "EVDCTR";
String wifiSsid        = "";
String wifiPassword    = "";
bool   useStaticIp     = false;
String staticIp        = "";
String staticGateway   = "";
String staticSubnet    = "";
String staticDns       = "";
bool   accessPointActive = false;
uint32_t serverEpoch   = 0;

// ---- Persistence -----------------------------------------------------------
bool     settingsDirty      = false;
unsigned long settingsDirtySince = 0;

// ---- Source-board UART (BT/AUX selection) ----------------------------------
HardwareSerial *receiverUart = nullptr;
String lastReceiverSource = "BT";
String bootSourceCStr() { return "BT"; }
String receiverBtJson = "{\"devices\":[]}";

// ---- DLNA ------------------------------------------------------------------
volatile bool dlnaPlaying = false;
volatile bool dlnaPaused  = false;
volatile int  dlnaVolume  = 100;
uint8_t dlnaPrevSource = SRC_FROM_LOCAL;

// ---- Downlink (EVDSTR control plane) --------------------------------------
// The last state STR reported, cached so the dashboard can read it without a
// round trip. Guarded by linkCtrlMutex.
struct DownlinkCache {
  bool     valid            = false;
  uint32_t updatedMs        = 0;
  uint32_t linkOkMs         = 0;
  uint32_t framesSent       = 0;
  uint32_t framesDropped    = 0;
  uint32_t subscribers      = 0;
  uint32_t queueDepth       = 0;
  uint32_t queueTarget      = 0;
  uint32_t pacerLateMs      = 0;
  int      wifiRssi         = 0;
  uint32_t heapFree         = 0;
  uint32_t heapMinFree      = 0;
  uint32_t upMs             = 0;
  String   strVersion       = "";
  String   strIp            = "";
  String   playersJson      = "[]";
  String   zonesJson        = "[]";
  String   configJson       = "{}";
};
DownlinkCache gDownlinkCache;
