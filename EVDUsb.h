#pragma once
// ============================================================================
//  EVDUsb.h - USB Audio Class (UAC1) capture for EVDCTR
// ============================================================================
//
//  The box enumerates as a 48 kHz / 16-bit / stereo *speaker* (UAC1 playback,
//  host -> device), so a PC or phone can send it audio as an ordinary sound
//  device. This is the "USB" input the dashboard's source selector picks, and
//  it feeds rawQueue like every other input.
//
//  ---- Which library, and why the API looks like this ----------------------
//
//  EspUsbDevice 3.x, by tanakamasayuki. 3.x is a rewrite: there is no
//  `device.task()` to call from loop(), no CFG_TUD_* build flags, and a
//  Function's own begin() is not public. The shape is:
//
//      declare  ->  configure()  ->  device.begin()  ->  read()/pollEvent()
//
//  Buffers are sized at RUN TIME by configure(), not by build flags, and are
//  allocated in begin() and freed in end(). So this file must call configure()
//  BEFORE begin() and must not touch the stream objects until begin() returned.
//
//  ---- Why the host volume is applied here --------------------------------
//
//  The library reports volume/mute as EVENTS and never applies them to the PCM
//  (its own docs say so twice). Because we advertise a Feature Unit, Windows
//  moves its slider with SET_CUR and leaves the PCM at full scale. So the
//  slider would do nothing audible unless we apply the gain ourselves.
//  readServerUsbAudio() therefore scales the samples by gUsbGainQ15.
//
//  ---- Threading ----------------------------------------------------------
//
//  startServerUsbAudio() is called from the HTTP task (source switch).
//  readServerUsbAudio() is called from audioReadTask on core 0. The library
//  locks its own read()/pollEvent() internally, so the extra lock here is only
//  to keep start/stop from running while a read is in flight.
//
//  ---- Wiring -------------------------------------------------------------
//
//      USB D-  GPIO19
//      USB D+  GPIO20
//
//  On most dev boards this is the second USB-C port, labelled "USB" or "OTG" to
//  distinguish it from the "UART" port used for flashing. Board setting
//  "USB Mode" must be USB-OTG (TinyUSB), which is `USBMode=default` in the FQBN.
// ============================================================================
#include <esp_heap_caps.h>

#if EVD_HAS_USB_DEVICE_LIB
#if __has_include(<EspUsbDevice.h>)
#include <EspUsbDevice.h>
#else
#undef EVD_HAS_USB_DEVICE_LIB
#define EVD_HAS_USB_DEVICE_LIB 0
#pragma message("EVDCTR: EspUsbDevice.h not found, USB input disabled")
#endif
#endif

// ---- Host volume / mute mirrors -------------------------------------------
// Written from audioReadTask only, read by /api/state for diagnostics.
volatile int16_t gUsbHostVolumeDb256      = 0;      // 1/256 dB, 0 = full scale
volatile bool    gUsbHostMuted            = false;
volatile bool    gUsbHostHasEverSetVolume = false;  // host has moved the slider
volatile bool    gUsbStreaming            = false;  // PCM has arrived recently

#if EVD_HAS_USB_DEVICE_LIB

// ---- Objects --------------------------------------------------------------
// Static, not heap: there is exactly one USB device in this firmware, and a
// failed new (std::nothrow) at 92 % heap is one more failure mode to reason
// about for no benefit.
static EspUsbDevice               gUsbDevice;
static EspUsbAudioFunction        gUsbAudio(gUsbDevice, EspUsbAudioProtocol::Uac1);
static EspUsbAudioPlaybackStream *gUsbPlayback = nullptr;

static SemaphoreHandle_t gUsbLifecycleMutex = nullptr;
static bool              gUsbRunning        = false;
static const char       *gUsbLastError      = "";

inline const char *usbLastError() { return gUsbLastError; }

// Called from setup(), before audioReadTask exists. Idempotent.
inline void initUsbLifecycle() {
  if (!gUsbLifecycleMutex) gUsbLifecycleMutex = xSemaphoreCreateMutex();
  if (!gUsbLifecycleMutex) LOGE("USB audio: lifecycle mutex alloc failed (OOM)");
}

// ---- Gain from the Feature Unit -------------------------------------------
// [0] master, [1] left, [2] right. Windows may use master or per-channel.
static int16_t gUsbChDb256[3] = {0, 0, 0};
static bool    gUsbChMuted[3] = {false, false, false};
static int32_t gUsbGainQ15    = 32768;   // 32768 = unity (Q15)

// Start at -20 dB (10 % amplitude) on every connect. A box that comes up at
// full scale into a live amplifier is a nasty surprise, and Windows reads the
// Feature Unit on enumeration so the slider agrees with what we applied.
static const int16_t EVD_USB_INITIAL_VOLUME_DB256 = -20 * 256;

inline void updateUsbGain() {
  // Master and the two channel values ADD in dB terms here only when the host
  // actually uses per-channel controls; Windows normally sends master only.
  bool muted = gUsbChMuted[0] || (gUsbChMuted[1] && gUsbChMuted[2]);
  gUsbHostMuted = muted;
  if (muted) { gUsbGainQ15 = 0; return; }

  int16_t side = gUsbChDb256[1] > gUsbChDb256[2] ? gUsbChDb256[1] : gUsbChDb256[2];
  float db = ((float)gUsbChDb256[0] + (float)side) / 256.0f;
  gUsbHostVolumeDb256 = (int16_t)(db * 256.0f);
  if (db >= 0.0f)        gUsbGainQ15 = 32768;
  else if (db <= -80.0f) gUsbGainQ15 = 0;
  else                   gUsbGainQ15 = (int32_t)(32768.0f * powf(10.0f, db / 20.0f));
}

// Re-read the Feature Unit state directly, so a slider move still takes effect
// if its event was dropped (the event queue is finite). Throttled to 50 ms.
static uint32_t gUsbLastVolPollMs = 0;
inline void refreshUsbVolumeState() {
  uint32_t now = millis();
  if (now - gUsbLastVolPollMs < 50) return;
  gUsbLastVolPollMs = now;
  bool changed = false;
  for (int i = 0; i < 3; ++i) {
    int16_t v = 0;
    bool    m = false;
    if (gUsbAudio.getVolume(v, EspUsbAudioDirection::Playback, (uint8_t)i) &&
        v != gUsbChDb256[i]) { gUsbChDb256[i] = v; changed = true; }
    if (gUsbAudio.getMute(m, EspUsbAudioDirection::Playback, (uint8_t)i) &&
        m != gUsbChMuted[i]) { gUsbChMuted[i] = m; changed = true; }
  }
  if (changed) { gUsbHostHasEverSetVolume = true; updateUsbGain(); }
}

inline void pollUsbAudioEvents() {
  if (!gUsbRunning) return;
  refreshUsbVolumeState();
  EspUsbAudioEvent ev;
  // Bounded drain: a pathological flood must not starve the read loop.
  for (int i = 0; i < 16 && gUsbAudio.pollEvent(ev); ++i) {
    // Capture-side changes are irrelevant to the playback path.
    if (ev.target == EspUsbAudioEventTarget::Capture) continue;

    switch (ev.type) {
      case EspUsbAudioEventType::VolumeChanged:
        if (ev.channel < 3) {
          gUsbChDb256[ev.channel]  = ev.volumeDb256;
          gUsbHostHasEverSetVolume = true;
          updateUsbGain();
        }
        LOGI("USB host volume (ch %u): %.2f dB (raw %d) -> gain %.1f%%",
             (unsigned)ev.channel, (float)ev.volumeDb256 / 256.0f,
             (int)ev.volumeDb256, (float)gUsbGainQ15 * 100.0f / 32768.0f);
        break;
      case EspUsbAudioEventType::MuteChanged:
        if (ev.channel < 3) {
          gUsbChMuted[ev.channel]  = ev.muted;
          gUsbHostHasEverSetVolume = true;
          updateUsbGain();
        }
        LOGI("USB host mute (ch %u): %s", (unsigned)ev.channel, ev.muted ? "on" : "off");
        break;
      case EspUsbAudioEventType::SampleRateChanged:
        // The resampler already handles 44.1 <-> 48 kHz and the source rate is
        // fixed by the source board, so this is log-only.
        LOGI("USB host sample rate: %u Hz", (unsigned)ev.sampleRate);
        break;
      case EspUsbAudioEventType::StreamStateChanged:
        LOGI("USB audio: stream %s (alt=%u)",
             ev.enabled ? "started" : "stopped", (unsigned)ev.alternateSetting);
        break;
    }
  }
}

// ---- startServerUsbAudio -------------------------------------------------
// Enumerates us as a UAC1 speaker at SOURCE_SAMPLE_RATE, 16-bit, stereo. The
// rate matches the I2S path exactly so the resampler ratio stays valid.
//
// Order matters in 3.x: addFormat() and configure() are only accepted while
// STOPPED, and device.begin() is what allocates the buffers and starts USB.
inline bool startServerUsbAudio() {
  if (gUsbRunning) return true;

  if (!gUsbLifecycleMutex) {
    LOGE("USB audio: start before initUsbLifecycle(); refusing");
    gUsbLastError = "mutex_missing";
    return false;
  }
  if (xSemaphoreTake(gUsbLifecycleMutex, portMAX_DELAY) != pdTRUE) {
    gUsbLastError = "mutex_fail";
    return false;
  }
  if (gUsbRunning) { xSemaphoreGive(gUsbLifecycleMutex); return true; }

  LOGI("USB audio: enumerating UAC1 speaker (%d Hz, 16-bit, %d ch), heap %u B",
       SOURCE_SAMPLE_RATE, CHANNELS, (unsigned)ESP.getFreeHeap());

#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1
  LOGW("USB audio: ARDUINO_USB_MODE=1 (hardware CDC/JTAG). Set USB Mode to "
       "'USB-OTG (TinyUSB)' or begin() will fail");
#endif

  // {sampleRate, channels, bytesPerSample, bitsPerSample}. The third field is
  // UAC's bSubFrameSize - bytes per channel - so 2 for 16-bit, not 4.
  gUsbPlayback = &gUsbAudio.addPlaybackStream();
  if (!gUsbPlayback->addFormat({(uint32_t)SOURCE_SAMPLE_RATE,
                                (uint8_t)CHANNELS, (uint8_t)2, (uint8_t)16})) {
    // addFormat() returns false for a rate/shape the descriptor cannot carry
    // (a packet over 1023 bytes at full speed) and leaves the reason in
    // device.lastError().
    LOGE("USB audio: addFormat failed: %s", gUsbDevice.lastErrorName());
    gUsbLastError = "format_failed";
    gUsbPlayback  = nullptr;
    xSemaphoreGive(gUsbLifecycleMutex);
    return false;
  }

  // Ring sizes. 20 ms of audio in flight is plenty: the resampler downstream
  // absorbs jitter, and a bigger ring only adds latency to a live input.
  // 48000 / 1000 * 20 = 960 frames.
  EspUsbAudioConfig acfg;
  acfg.eventCapacity          = 16;
  acfg.playbackBufferFrames   = 960;
  acfg.playbackOverflowPolicy = EspUsbAudioOverflowPolicy::DropOldest;
  esp_err_t err = gUsbAudio.configure(acfg);
  if (err != ESP_OK) {
    LOGE("USB audio: configure failed: %s", esp_err_to_name(err));
    gUsbLastError = "configure_failed";
    gUsbPlayback  = nullptr;
    xSemaphoreGive(gUsbLifecycleMutex);
    return false;
  }

  EspUsbDeviceConfig cfg;
  cfg.vid          = EVD_USB_VID;
  cfg.pid          = EVD_USB_PID;
  cfg.manufacturer = "Enkelvoud";
  cfg.product      = "Enkelvoud CTR";
  cfg.serialNumber = "evdctr-usb";

  if (!gUsbDevice.begin(cfg)) {
    LOGE("USB audio: device.begin() FAILED: %s (heap %u B, VID:PID %04x:%04x)",
         gUsbDevice.lastErrorName(), (unsigned)ESP.getFreeHeap(),
         (unsigned)EVD_USB_VID, (unsigned)EVD_USB_PID);
    LOGE("USB audio: ESP_ERR_INVALID_SIZE = endpoints/FIFO do not fit; "
         "ESP_ERR_INVALID_STATE = wrong USB mode (needs USB-OTG/TinyUSB)");
    gUsbLastError = "begin_failed";
    gUsbPlayback  = nullptr;
    xSemaphoreGive(gUsbLifecycleMutex);
    return false;
  }

  // Reset the Feature Unit mirrors, then set the starting volume. The audio
  // graph exists only after begin(), so the accessors are valid from here on.
  gUsbHostVolumeDb256      = 0;
  gUsbHostMuted            = false;
  gUsbHostHasEverSetVolume = false;
  gUsbGainQ15              = 32768;
  for (int i = 0; i < 3; ++i) { gUsbChDb256[i] = 0; gUsbChMuted[i] = false; }

  {
    const bool hasMaster = gUsbAudio.hasVolume(EspUsbAudioDirection::Playback, 0);
    for (int i = 0; i < 3; ++i) {
      if (hasMaster ? (i != 0) : (i == 0)) continue;
      gUsbAudio.setMute(false, EspUsbAudioDirection::Playback, (uint8_t)i);
      if (gUsbAudio.setVolume(EVD_USB_INITIAL_VOLUME_DB256,
                              EspUsbAudioDirection::Playback, (uint8_t)i))
        gUsbChDb256[i] = EVD_USB_INITIAL_VOLUME_DB256;
    }
  }
  gUsbHostHasEverSetVolume = true;
  gUsbLastVolPollMs        = 0;
  updateUsbGain();
  gUsbAudio.clearEvents();

  EspUsbAudioVolumeRange range;
  if (gUsbAudio.getVolumeRange(range)) {
    LOGI("USB audio: Feature Unit volume %d..%d (1/256 dB), res %d",
         (int)range.min, (int)range.max, (int)range.resolution);
  } else {
    LOGW("USB audio: no volume range - the host slider will be a no-op");
  }
  LOGI("USB audio: initial volume %.2f dB", (float)gUsbHostVolumeDb256 / 256.0f);

  gUsbLastError = "";
  gUsbRunning   = true;
  xSemaphoreGive(gUsbLifecycleMutex);

  LOGI("USB audio: enumerated %04x:%04x, heap after begin %u B - plug the host "
       "into the USB-OTG port",
       (unsigned)EVD_USB_VID, (unsigned)EVD_USB_PID, (unsigned)ESP.getFreeHeap());
  return true;
}

// ---- stopServerUsbAudio --------------------------------------------------
// The host sees the device disappear, which is the correct UX: while the source
// is BT/AUX we are not a USB sound card.
//
// The lock take is BOUNDED. If a read is in flight we publish not-running and
// return without calling end(): end() frees the buffers the in-flight read is
// using. The device stays up until the next start, which is a far better
// outcome than a use-after-free.
inline void stopServerUsbAudio() {
  if (!gUsbRunning) return;
  LOGI("USB audio: stopping device");

  if (!gUsbLifecycleMutex) {
    gUsbRunning   = false;
    gUsbPlayback  = nullptr;
    gUsbStreaming = false;
    return;
  }
  if (xSemaphoreTake(gUsbLifecycleMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    LOGE("USB audio: stop timed out waiting for read(); leaving the device up");
    gUsbRunning   = false;
    gUsbStreaming = false;
    return;
  }

  gUsbRunning = false;
  gUsbDevice.end();          // frees the rings and the packet DMA areas
  gUsbPlayback  = nullptr;
  gUsbStreaming = false;
  gUsbHostVolumeDb256      = 0;
  gUsbHostMuted            = false;
  gUsbHostHasEverSetVolume = false;
  xSemaphoreGive(gUsbLifecycleMutex);
  LOGI("USB audio: device stopped");
}

// ---- readServerUsbAudio --------------------------------------------------
// Pulls PCM from the UAC playback stream into the RawChunk the audio task
// pushes to rawQueue. Returns bytes written (whole stereo 16-bit frames only).
//
// The gain is applied HERE, not by the host: see the note at the top of file.
inline size_t readServerUsbAudio(RawChunk &out) {
  out.len = 0;
  if (!gUsbRunning || !gUsbPlayback) return 0;
  if (!gUsbLifecycleMutex) return 0;
  // Non-blocking: a stop in progress means skip this read entirely; the audio
  // task just sees 0 bytes and sleeps.
  if (xSemaphoreTake(gUsbLifecycleMutex, 0) != pdTRUE) return 0;

  size_t n = 0;
  if (gUsbRunning && gUsbPlayback) {
    pollUsbAudioEvents();

    // Read straight into the chunk: one copy fewer than a staging buffer, and
    // RawChunk::data is already the right size.
    size_t cap = sizeof(out.data);
    if (cap > (size_t)I2S_READ_CHUNK_BYTES) cap = I2S_READ_CHUNK_BYTES;
    n = gUsbPlayback->read(out.data, cap);

    if (n >= 2 && gUsbHostHasEverSetVolume) {
      int32_t g = gUsbGainQ15;
      if (g < 32768) {
        int16_t *s = (int16_t *)out.data;
        size_t cnt = n / 2;
        for (size_t i = 0; i < cnt; ++i)
          s[i] = (int16_t)(((int32_t)s[i] * g) >> 15);
      }
    }
    n &= ~3u;   // whole stereo 16-bit frames
    if (n) {
      out.len         = (uint32_t)n;
      out.captured_ms = millis();
      gUsbStreaming   = true;
    }
  }
  xSemaphoreGive(gUsbLifecycleMutex);
  return n;
}

// Kept for callers that only want the diagnostics refreshed.
inline void serviceUsbPoll() { pollUsbAudioEvents(); }

#else  // EVD_HAS_USB_DEVICE_LIB == 0

// ---- No-op fallbacks -----------------------------------------------------
// The firmware builds and runs without the library; selecting USB on the
// dashboard is refused by the source handler with a 501 and a clear log line.
inline void   initUsbLifecycle()    {}
inline bool   startServerUsbAudio() {
  LOGE("USB audio unavailable: firmware built without the EspUsbDevice library");
  return false;
}
inline const char *usbLastError() { return "no_library"; }
inline void   stopServerUsbAudio()  {}
inline size_t readServerUsbAudio(RawChunk &out) { out.len = 0; return 0; }
inline void   pollUsbAudioEvents()  {}
inline void   serviceUsbPoll()      {}

#endif
