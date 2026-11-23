#pragma once
// ============================================================================
//  EVDMain.h - setup() / loop() for EVDCTR
//
//  NOTE: setup() and loop() MUST NOT be 'inline'. The Arduino core's main.cpp
//  references them as external symbols.
// ============================================================================
#include <LittleFS.h>
#include <esp_system.h>

// ---- Boot banner -----------------------------------------------------------
// Everything an operator needs in the first second of a serial monitor, in the
// order they need it: which board and firmware, why it last rebooted, how the
// radio came up (the line that says "AP fallback ... 192.168.4.1" is the one
// that answers "why can I not see it on my network"), and the compile-time
// constants that a stale binary would get wrong.
//
// Printed with Serial.printf and not LOGI: this runs before gSerialLogLevel is
// meaningful, and it must never be filtered out by a level left over from the
// previous boot.
inline void printBootBanner(const char *phase) {
  Serial.printf("\n");
  Serial.printf("+------------------------------------------------------+\n");
  Serial.printf("|  EVDCTR  %-10s  capture / encode half            |\n", EVD_FW_VERSION);
  Serial.printf("|  %-50s|\n", phase);
  Serial.printf("+------------------------------------------------------+\n");
}

inline const char *resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON (cold boot)";
    case ESP_RST_SW:        return "SOFTWARE (esp_restart)";
    case ESP_RST_PANIC:     return "PANIC (exception - see backtrace above)";
    case ESP_RST_INT_WDT:   return "INT_WDT (interrupt watchdog)";
    case ESP_RST_TASK_WDT:  return "TASK_WDT (a task blocked too long)";
    case ESP_RST_WDT:       return "WDT (other watchdog)";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP wake";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (power supply too weak)";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

inline void setup_serial() {
  // CDCOnBoot=cdc: this is the USB-Serial-JTAG port, not a UART bridge. It
  // only appears on the host once the sketch is running, so a monitor opened
  // before the reset waits for it - and everything printed before begin()
  // is lost. 115200 8N1, no flow control.
  Serial.begin(LOG_BAUD);
  delay(300);

  printBootBanner("booting");
  Serial.printf("[BOOT] firmware      : EVDCTR %s\n", EVD_FW_VERSION);
  Serial.printf("[BOOT] reset reason  : %s\n", resetReasonStr(esp_reset_reason()));
  Serial.printf("[BOOT] chip          : %s rev %d, %d MHz, %d core(s)\n",
                ESP.getChipModel(), (int)ESP.getChipRevision(),
                (int)getCpuFrequencyMhz(), (int)ESP.getChipCores());
  Serial.printf("[BOOT] flash         : %u MB, sketch %u KB\n",
                (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)),
                (unsigned)(ESP.getSketchSize() / 1024));
  Serial.printf("[BOOT] psram         : %u KB free\n",
                (unsigned)(ESP.getFreePsram() / 1024));
  Serial.printf("[BOOT] free heap     : %u KB\n", (unsigned)(ESP.getFreeHeap() / 1024));
  Serial.printf("[BOOT] dashboard     : %u B gzip (EVDCTRL.h via gen_gz.py)\n",
                (unsigned)EVDCTRL_GZ_LEN);
  Serial.printf("[BOOT] encoder       : %d bps, %u Hz opus, %d ms frames, %d ch\n",
                OPUS_BITRATE, (unsigned)OPUS_SAMPLE_RATE_DEFAULT, OPUS_FRAME_MS,
                CHANNELS);
  Serial.printf("[BOOT] pin link      : SCLK %d MOSI %d IRQ %d CS %d @ %lu Hz (master)\n",
                PIN_LINK_SCLK, PIN_LINK_MOSI, PIN_LINK_IRQ, PIN_LINK_CS,
                (unsigned long)LINK_SPI_HZ);
  Serial.printf("[BOOT] discovery     : UDP %d, probe every %d ms while unpaired\n",
                DISCOVERY_PORT, DISCOVERY_TX_MS);
  Serial.printf("[BOOT] USB audio     : %s\n",
                EVD_USB_INPUT_BUILT ? "compiled in (EspUsbDevice)" : "NOT COMPILED IN");
  Serial.printf("\n");
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
      // Scoped so the guard's destructor runs: it closes the NVS namespace and
      // releases prefsMutex. ESP.restart() never returns, so a guard still in
      // scope would leave the namespace uncommitted and the mutex taken.
      {
        PrefsGuard g("evdctr", false);
        if (g.ok) g.prefs.clear();
      }
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
  if (wifiSsid.length() && !wifiPassword.length()) {
    // A saved SSID with no password is almost always a password that was
    // cleared by a failed save, not an open network. Say so: this is the most
    // common reason a board falls back to its AP while the operator believes
    // it is configured.
    Serial.printf("[BOOT] NOTE          : ssid '%s' has NO password saved - "
                  "if that network is protected, the join will fail and this "
                  "board will open its own AP\n", wifiSsid.c_str());
  }
  Serial.printf("[BOOT] stored config : name '%s', ssid '%s', static %s\n",
                serverName.c_str(),
                wifiSsid.length() ? wifiSsid.c_str() : "(none - AP fallback)",
                useStaticIp ? staticIp.c_str() : "no");

  // The radio comes up BEFORE the audio pipeline and the HTTP server, so the
  // banner below is the first thing an operator reads and the last thing that
  // can go wrong silently.
  Serial.printf("[BOOT] wifi          : bringing up (timeout %d ms)\n",
                WIFI_CONNECT_TIMEOUT_MS);
  setup_wifi_sta();
  if (accessPointActive) {
    Serial.printf("[BOOT] wifi          : AP FALLBACK '%s' at http://%s/\n",
                  WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
    if (!wifiSsid.length())
      Serial.printf("[BOOT] reason        : no Wi-Fi credentials stored yet\n");
    else
      Serial.printf("[BOOT] reason        : could not join '%s' within %d ms "
                    "(status %d) - wrong password, out of range, or 5 GHz only\n",
                    wifiSsid.c_str(), WIFI_CONNECT_TIMEOUT_MS, (int)WiFi.status());
    Serial.printf("[BOOT] next          : join '%s' (no password), open "
                  "http://%s/ and enter your Wi-Fi\n",
                  WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
  } else {
    Serial.printf("[BOOT] wifi          : connected to '%s', ip %s, rssi %d dBm\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                  (int)WiFi.RSSI());
    Serial.printf("[BOOT] dashboard     : http://%s/  or  http://%s.local/\n",
                  WiFi.localIP().toString().c_str(), serverName.c_str());
  }
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

  // Before setup_queues_and_tasks(): audioReadTask exists from that call on,
  // and the USB lifecycle mutex must exist before anything can take it.
  initUsbLifecycle();

  setup_queues_and_tasks();

  logLinkPeerState("boot");
  printBootBanner("boot complete");
  Serial.printf("[BOOT] reach me at  : http://%s/  (pair EVDSTR on the Stream tab)\n",
                accessPointActive ? WiFi.softAPIP().toString().c_str()
                                  : WiFi.localIP().toString().c_str());
  Serial.printf("[BOOT] steady state : errors only; raise the level with "
                "GET /api/log/level?level=3 or the Log tab\n\n");
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

  // ---- 10 s heartbeat ------------------------------------------------------
  // A board that is up but doing nothing useful looks exactly like a healthy
  // one on the network. This line is the difference: it says whether audio is
  // arriving, whether the link to EVDSTR is carrying it, and whether the
  // pairing has happened. It prints at level 1 (steady state) on purpose -
  // one line every 10 s is not noise, and without it a silent box has nothing
  // to say in a serial monitor.
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat > 10000) {
    lastBeat = millis();
    Serial.printf("[HB] up %lus | heap %uK | wifi %s %d dBm | src %s | "
                  "enc %lu f | link %s (%lu out, %lu drop) | str %s\n",
                  (unsigned long)(millis() / 1000),
                  (unsigned)(ESP.getFreeHeap() / 1024),
                  accessPointActive ? "AP" : WiFi.SSID().c_str(), (int)WiFi.RSSI(),
                  serverInputSource == SRC_FROM_USB ? "USB"
                    : (serverInputSource == SRC_FROM_NETWORK ? "DLNA" : "I2S"),
                  (unsigned long)statOpusEncoded,
                  gLinkHealthy ? "ok" : "IDLE",
                  (unsigned long)statLinkSent, (unsigned long)statLinkDropped,
                  strHost.length() ? strHost.c_str() : "(unpaired)");
    if (!gLinkHealthy) logLinkPeerState("heartbeat");
  }

  delay(10);
}
