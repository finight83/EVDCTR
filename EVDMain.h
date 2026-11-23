#pragma once
// ============================================================================
//  EVDMain.h - setup() / loop() for EVDCTR
//
//  NOTE: setup() and loop() MUST NOT be 'inline'. The Arduino core's main.cpp
//  references them as external symbols.
// ============================================================================
#include <LittleFS.h>
#include <esp_system.h>

inline void setup_serial() {
  Serial.begin(LOG_BAUD);
  delay(300);
  LOGI("========================================================");
  LOGI("EVDCTR %s - capture/encode half of the split server", EVD_FW_VERSION);
  LOGI("========================================================");
  esp_reset_reason_t reason = esp_reset_reason();
  const char *why = "UNKNOWN";
  switch (reason) {
    case ESP_RST_POWERON:  why = "POWERON";     break;
    case ESP_RST_SW:       why = "SOFTWARE";    break;
    case ESP_RST_PANIC:    why = "PANIC";       break;
    case ESP_RST_INT_WDT:  why = "INT_WDT";     break;
    case ESP_RST_TASK_WDT: why = "TASK_WDT";    break;
    case ESP_RST_BROWNOUT: why = "BROWNOUT";    break;
    default: break;
  }
  LOGI("reset reason: %s", why);
}

inline void createSyncPrimitives() {
  prefsMutex           = xSemaphoreCreateMutex();
  uartMutex            = xSemaphoreCreateMutex();
  linkCtrlMutex        = xSemaphoreCreateMutex();
  logTailMutex         = xSemaphoreCreateMutex();
  gUsbLifecycleMutex   = xSemaphoreCreateMutex();
  if (!prefsMutex || !uartMutex || !linkCtrlMutex || !logTailMutex ||
      !gUsbLifecycleMutex) {
    LOGE("mutex creation failed");
  }
}

// ---- Deferred NVS flush ----------------------------------------------------
// Zone/config writes never happen on the HTTP task: a 4 KB sector erase stalls
// the flash cache for ~100 ms, and that would land inside an audio chunk on
// this board. loop() flushes once the operator has stopped moving sliders.
inline void serviceDeferredSettingsFlush() {
  if (!settingsDirty) return;
  if (millis() - settingsDirtySince < NVM_SAVE_DELAY_MS) return;
  settingsDirty = false;
  // Only the downlink settings are deferred on this board. Rewriting the WiFi
  // credentials here meant a pairing change also rewrote ssid/pass - and if it
  // ran while an in-flight /save had cleared the password field, it persisted
  // an empty password. WiFi settings are saved by handleWiFiSave(), which is
  // the only place that changes them.
  saveDownlinkSettings();     // the EVDSTR pairing address
}

// ---- Factory reset ---------------------------------------------------------
inline void serviceFactoryResetPin() {
  static unsigned long heldSince = 0;
  pinMode(PIN_FACTORY_RESET, INPUT_PULLUP);
  if (digitalRead(PIN_FACTORY_RESET) == LOW) {
    if (heldSince == 0) heldSince = millis();
    if (millis() - heldSince > FACTORY_RESET_HOLD_MS) {
      LOGW("factory reset: clearing NVS and restarting");
      PrefsGuard g("evdctr", false);
      if (g.ok) g.prefs.clear();
      delay(300);
      ESP.restart();
    }
  } else {
    heldSince = 0;
  }
}

// ---- Link watchdog ---------------------------------------------------------
// If no frame has crossed for a second while audio is running, the link is
// down: log it once, not 50 times a second, and keep going. The dashboard's
// Stream tab shows it as "link: down" with the CS-timeout counter, which is
// the diagnostic that distinguishes "STR not booted" from "wrong pins".
inline void serviceLinkWatchdog() {
  static unsigned long lastWarn = 0;
  static bool wasHealthy = true;
  bool healthy = gLinkHealthy;
  if (healthy == wasHealthy) return;
  wasHealthy = healthy;
  if (!healthy && millis() - lastWarn > 5000) {
    lastWarn = millis();
    LOGW("pin link DOWN: csTimeouts=%u dropped=%u (is EVDSTR running?)",
         (unsigned)statLinkCsTimeouts, (unsigned)statLinkDropped);
  } else if (healthy) {
    LOGI("pin link UP again");
  }
}

// ---- USB-mode producer -----------------------------------------------------
// audioReadTask yields in USB mode; this is where the UAC1 stream is drained
// and pushed into rawQueue, on core 0 like every other producer.
inline void serviceUsbAudio() {
  if (serverInputSource != SRC_FROM_USB) return;
  static RawChunk chunk;
  size_t n = readServerUsbAudio(chunk);
  if (n == 0) return;
  if (xQueueSend(rawQueue, &chunk, pdMS_TO_TICKS(20)) != pdTRUE) statRawDropped++;
}

// ============================================================================
//  setup()
// ============================================================================
void setup() {
  setup_serial();
  createSyncPrimitives();

  if (!LittleFS.begin(true)) {
    LOGE("LittleFS mount failed (OTA spool unavailable)");
  } else {
    LOGI("LittleFS: %u bytes free", (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()));
  }

  loadWiFiSettings();
  loadDownlinkSettings();

  setup_wifi_sta();
  serverEpoch = esp_random();

  // Encoder first: audioEncodeTask needs it on its very first frame.
  setup_opus(OPUS_SAMPLE_RATE_DEFAULT);
  configureQuietFilters();

  setupDiscoveryResponder();

  // Pin link before the encode task exists, so the bus is initialised on
  // core 0 and then owned by core 1 with no race.
  if (!setupLinkSpi()) {
    LOGE("pin link init FAILED - audio will encode but not reach EVDSTR");
  } else {
    LOGI("pin link ready: SCLK %d MOSI %d IRQ %d CS %d @ %lu Hz",
         PIN_LINK_SCLK, PIN_LINK_MOSI, PIN_LINK_IRQ, PIN_LINK_CS,
         (unsigned long)LINK_SPI_HZ);
  }

  setup_http_server();
  initDlna();
  setupReceiverLink();

  if (!setup_i2s_in()) {
    LOGE("I2S input unavailable; USB/DLNA inputs still work");
  }

  setup_queues_and_tasks();

  LOGI("boot complete: http://%s/  (pair EVDSTR on the Stream tab)",
       WiFi.localIP().toString().c_str());
}

// ============================================================================
//  loop() - core 0, priority 1. Nothing here is on the audio critical path.
// ============================================================================
void loop() {
  serviceFactoryResetPin();
  serviceVolumePot();
  serviceDiscovery();
  serviceReceiverLink();
  serviceUsbAudio();
  pollUsbAudioEvents();
  serviceDeferredSettingsFlush();
  serviceLinkWatchdog();

  // Refresh the STR summary at 2 s. Cheap (one small GET) and it is what the
  // dashboard's Stream tab reads; /api/state never blocks on it.
  static unsigned long lastPull = 0;
  if (millis() - lastPull > 2000) {
    lastPull = millis();
    refreshDownlinkCache();
  }

  delay(10);
}
