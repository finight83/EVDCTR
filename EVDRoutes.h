#pragma once
// ============================================================================
//  EVDRoutes.h - EVDCTR HTTP route table
// ============================================================================
//
//  Every handler here runs on the AsyncTCP worker (core 0). The rule for this
//  board: a route either (a) touches only CTR state, or (b) is a relay to
//  EVDSTR through EVDDownlink.h. There is no route that touches a player
//  directly, because no player can reach this board.
//
//  Route map
//  ---------
//   GET  /                        dashboard (gzip, EVDCTRL_GZ.h)
//   GET  /settings                network setup page (AP fallback)
//   POST /save                    network settings
//   GET  /api/network/state       current network
//   GET  /api/network-test        connectivity test
//   GET  /api/wifi/scan/start|status
//   POST /api/restart
//   GET  /api/state               CTR capture/encode state + downlink summary
//   GET  /api/stats               counters + per-task CPU + heap/PSRAM
//   GET  /api/log/tail            log ring
//   GET  /api/log/level  POST /api/log/level
//   GET  /api/link/state          downlink + pin-link health
//   POST /api/link/pair           set STR address
//   GET  /api/link/players        relayed from STR
//   GET  /api/link/zones          relayed from STR
//   POST /api/config              relayed to STR  (volume, mute, mode, buffer)
//   POST /api/audio/encoding      applied HERE (the encoder lives here)
//   POST /api/audio/encoding/revert | /defaults
//   GET  /api/audio/encoding      CTR's live encoder values
//   POST /api/zones/*             relayed to STR
//   POST /api/source | /api/cmd   source board over UART (CTR-owned)
//   GET  /api/bt/devices          source board BT list
//   GET  /description.xml, /upnp/*  DLNA
//   GET  /api/ota/*               CTR self-OTA
// ============================================================================
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include "EVDOTA.h"

AsyncWebServer server(80);

// ---- helpers ---------------------------------------------------------------
inline void sendJson(AsyncWebServerRequest *r, const String &body) {
  AsyncWebServerResponse *res = r->beginResponse(200, "application/json", body);
  res->addHeader("Cache-Control", "no-store, max-age=0");
  r->send(res);
}

// ---- /api/state ------------------------------------------------------------
// Capture side first (what this board is doing), then a compact downlink
// block (what STR last reported). The dashboard's Status tab renders both.
inline void handleApiState(AsyncWebServerRequest *request) {
  static const char *srcStr[] = { "local", "usb", "network" };
  String j = "{";
  j += "\"role\":\"ctr\",\"version\":\"" EVD_FW_VERSION "\",";
  j += "\"name\":\"" + jsonEscape(serverName) + "\",";
  j += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  j += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  j += "\"upMs\":" + String((unsigned long)millis()) + ",";
  j += "\"source\":\"" + String(srcStr[serverInputSource <= 2 ? serverInputSource : 0]) + "\",";
  j += "\"volume\":" + String((int)masterVolume) + ",";
  j += "\"muted\":" + String(masterMuted ? "true" : "false") + ",";
  j += "\"mode\":\"" + String(playbackModeCStr()) + "\",";
  j += "\"quiet\":" + String(quietModeEnabled ? "true" : "false") + ",";
  j += "\"opusSampleRate\":" + String((unsigned)g_opusSampleRate) + ",";
  j += "\"opusFrameSamples\":" + String((unsigned)g_opusFrameSamples) + ",";
  j += "\"sourceSampleRate\":" + String((unsigned)g_sourceSampleRate) + ",";
  j += "\"encBitrate\":" + String(g_currentBitrate) + ",";
  j += "\"encComplexity\":" + String(currentOpusComplexity) + ",";
  j += "\"encVbr\":" + String(g_currentVbr ? "true" : "false") + ",";
  j += "\"encVbrConstraint\":" + String(g_currentVbrConstraint ? "true" : "false") + ",";
  j += "\"encSignal\":\"" + String(g_currentSignal == OPUS_SIGNAL_MUSIC ? "music" :
                                    g_currentSignal == OPUS_SIGNAL_VOICE ? "voice" : "auto") + "\",";
  j += "\"encApplication\":\"" + String(g_currentApplication == OPUS_APPLICATION_AUDIO ? "audio" :
                                        g_currentApplication == OPUS_APPLICATION_VOIP ? "voip" : "lowdelay") + "\",";
  j += "\"heapFree\":" + String((unsigned long)ESP.getFreeHeap()) + ",";
  j += "\"heapMinFree\":" + String((unsigned long)ESP.getMinFreeHeap()) + ",";
  j += "\"psramFree\":" + String((unsigned long)ESP.getFreePsram()) + ",";
  j += "\"linkHealthy\":" + String(gLinkHealthy ? "true" : "false") + ",";
  j += "\"linkSent\":" + String((unsigned long)statLinkSent) + ",";
  j += "\"linkDropped\":" + String((unsigned long)statLinkDropped) + ",";
  j += "\"linkCsTimeouts\":" + String((unsigned long)statLinkCsTimeouts) + ",";
  j += "\"dlnaPlaying\":" + String(dlnaPlaying ? "true" : "false") + ",";
  j += "\"usbStreaming\":" + String(gUsbStreaming ? "true" : "false") + ",";
  // Downlink summary: what STR said last time we asked.
  j += "\"downlink\":{\"paired\":" + String(strHost.length() ? "true" : "false") +
       ",\"host\":\"" + jsonEscape(strHost) + "\"" +
       ",\"cached\":" + (gDownlinkCache.valid ? "true" : "false") +
       ",\"subscribers\":" + String((unsigned long)gDownlinkCache.subscribers) +
       ",\"queueDepth\":" + String((unsigned long)gDownlinkCache.queueDepth) +
       ",\"queueTarget\":" + String((unsigned long)gDownlinkCache.queueTarget) +
       ",\"pacerLateMs\":" + String((unsigned long)gDownlinkCache.pacerLateMs) +
       ",\"strVersion\":\"" + jsonEscape(gDownlinkCache.strVersion) + "\"" +
       ",\"strIp\":\"" + jsonEscape(gDownlinkCache.strIp) + "\"}";
  j += "}";
  sendJson(request, j);
}

// ---- /api/stats ------------------------------------------------------------
inline void handleApiStats(AsyncWebServerRequest *request) {
  String j = "{";
  j += "\"role\":\"ctr\",";
  j += "\"opusEncoded\":" + String((unsigned long)statOpusEncoded) + ",";
  j += "\"opusDropped\":" + String((unsigned long)statOpusDropped) + ",";
  j += "\"rawDropped\":" + String((unsigned long)statRawDropped) + ",";
  j += "\"encodeOverruns\":" + String((unsigned long)statEncodeOverrunsTotal) + ",";
  j += "\"encodePeakUsLastSec\":" + String((unsigned long)statEncodePeakUsLastSec) + ",";
  j += "\"avgEncodeUs\":" + String((unsigned long)gAvgEncodeUs) + ",";
  j += "\"pendingDropped\":" + String((unsigned long)statPendingBufDroppedTotal) + ",";
  j += "\"pendingPeakDepth\":" + String((unsigned long)statPendingBufPeakDepthLastSec) + ",";
  j += "\"driftPpm\":" + String((unsigned long)statDriftPpm) + ",";
  j += "\"linkSent\":" + String((unsigned long)statLinkSent) + ",";
  j += "\"linkDropped\":" + String((unsigned long)statLinkDropped) + ",";
  j += "\"linkCsTimeouts\":" + String((unsigned long)statLinkCsTimeouts) + ",";
  j += "\"linkSilence\":" + String((unsigned long)statLinkSilence) + ",";
  j += "\"linkHealthy\":" + String(gLinkHealthy ? "true" : "false") + ",";
  j += "\"heapFree\":" + String((unsigned long)ESP.getFreeHeap()) + ",";
  j += "\"heapMinFree\":" + String((unsigned long)ESP.getMinFreeHeap()) + ",";
  j += "\"psramFree\":" + String((unsigned long)ESP.getFreePsram()) + ",";
  j += "\"uptimeMs\":" + String((unsigned long)millis());
  j += "}";
  sendJson(request, j);
}

// ---- /api/log --------------------------------------------------------------
inline void handleLogTail(AsyncWebServerRequest *request) {
  uint32_t since = request->hasParam("since")
                   ? (uint32_t)request->getParam("since")->value().toInt() : 0;
  String body = "{\"ok\":true,\"lines\":[";
  bool first = true;
  portENTER_CRITICAL(&gLogRingMux);
    uint32_t end = gLogRingSeq;
    uint32_t oldest = (end >= EVD_LOG_RING_LEN) ? (end - EVD_LOG_RING_LEN + 1) : 1;
    uint32_t start = since + 1;
    if (start < oldest) start = oldest;
    for (uint32_t s = start; s <= end; ++s) {
      const LogRingEntry &e = gLogRing[(s - 1) % EVD_LOG_RING_LEN];
      if (e.seq != s) continue;
      if (!first) body += ",";
      first = false;
      body += "{\"seq\":" + String(e.seq) + ",\"ms\":" + String(e.ms) +
              ",\"level\":\"" + String(e.level) + "\",\"text\":\"" +
              jsonEscape(String(e.text)) + "\"}";
    }
    body += "],\"nextSeq\":" + String(end) + "}";
  portEXIT_CRITICAL(&gLogRingMux);
  sendJson(request, body);
}

// ---- encoder controls (applied locally: the encoder is here) ---------------
inline void handleGetAudioEncoding(AsyncWebServerRequest *request) {
  String j = "{\"ok\":true,";
  j += "\"bitrate\":" + String(g_currentBitrate) + ",";
  j += "\"complexity\":" + String(currentOpusComplexity) + ",";
  j += "\"vbr\":" + String(g_currentVbr ? "true" : "false") + ",";
  j += "\"vbrConstraint\":" + String(g_currentVbrConstraint ? "true" : "false") + ",";
  j += "\"signal\":\"" + String(g_currentSignal == OPUS_SIGNAL_MUSIC ? "music" :
                                g_currentSignal == OPUS_SIGNAL_VOICE ? "voice" : "auto") + "\",";
  j += "\"application\":\"" + String(g_currentApplication == OPUS_APPLICATION_AUDIO ? "audio" :
                                     g_currentApplication == OPUS_APPLICATION_VOIP ? "voip" : "lowdelay") + "\",";
  j += "\"sampleRate\":" + String((unsigned)g_opusSampleRate) + ",";
  j += "\"bufferMs\":" + String((unsigned)audioBufferMs) + ",";
  j += "\"maxBitrate\":192000,\"minBitrate\":6000}";
  sendJson(request, j);
}

inline void handlePostAudioEncoding(AsyncWebServerRequest *request,
                                    uint8_t *data, size_t len,
                                    size_t index, size_t total) {
  const char *body = nullptr; size_t bodyLen = 0;
  if (!accumulateReqBody(request, data, len, index, total, 1024,
                         "{\"ok\":false,\"error\":\"payload too large\"}",
                         body, bodyLen)) return;
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, body, bodyLen)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  const bool wantsSave = doc["save"] | false;

  if (doc["bitrate"].is<int>()) {
    int b = doc["bitrate"];
    // 192 kbps is the ceiling that keeps a VBR peak inside one 1024-byte
    // link payload; above it the frame is dropped at the link and the
    // operator hears a click on every transient.
    if (b >= 6000 && b <= 510000) g_newBitrate = b > 192000 ? 192000 : b;
  }
  if (doc["complexity"].is<int>()) {
    int c = doc["complexity"];
    if (c >= 0 && c <= 10) g_pendingComplexity = c;
  }
  if (doc["vbr"].is<bool>())           g_newVbr = doc["vbr"] ? 1 : 0;
  if (doc["vbrConstraint"].is<bool>()) g_newVbrConstraint = doc["vbrConstraint"] ? 1 : 0;
  if (doc["signal"].is<const char*>()) {
    String s = doc["signal"].as<String>();
    if      (s == "music") g_newSignal = OPUS_SIGNAL_MUSIC;
    else if (s == "voice") g_newSignal = OPUS_SIGNAL_VOICE;
    else if (s == "auto")  g_newSignal = OPUS_AUTO;
  }
  if (doc["application"].is<const char*>()) {
    String a = doc["application"].as<String>();
    if      (a == "audio")    g_newApplication = OPUS_APPLICATION_AUDIO;
    else if (a == "voip")     g_newApplication = OPUS_APPLICATION_VOIP;
    else if (a == "lowdelay") g_newApplication = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
  }
  if (doc["sampleRate"].is<int>()) {
    int r = doc["sampleRate"];
    if ((r == OPUS_SAMPLE_RATE_48K || r == OPUS_SAMPLE_RATE_24K) &&
        (uint32_t)r != g_opusSampleRate) {
      g_pendingOpusSampleRate = (uint32_t)r;
    }
  }
  // Buffer latency is a *stream* setting: it belongs to the players, so it is
  // stored here for the dashboard and forwarded to STR with every config push.
  if (doc["bufferMs"].is<int>()) {
    int bm = doc["bufferMs"];
    if (bm >= 20 && bm <= 600) audioBufferMs = (uint16_t)bm;
  }

  g_encoderSettingsDirty = true;
  if (wantsSave) {
    captureLiveIntoMode((ModeIdx)playbackModeIdx);
    settingsDirty      = true;
    settingsDirtySince = millis();
  }
  handleGetAudioEncoding(request);
}

inline void handlePostAudioEncodingRevert(AsyncWebServerRequest *request) {
  applyModeSettingsToLive((ModeIdx)playbackModeIdx);
  request->send(200, "application/json", "{\"ok\":true,\"reverted\":true}");
}

inline void handlePostAudioEncodingDefaults(AsyncWebServerRequest *request) {
  ModeEncSettings d = defaultPreset((ModeIdx)playbackModeIdx);
  saveModePreset((ModeIdx)playbackModeIdx, d);
  applyModeSettingsToLive((ModeIdx)playbackModeIdx);
  request->send(200, "application/json", "{\"ok\":true,\"defaults\":true}");
}

// ---- source board (CTR-owned) ---------------------------------------------
inline void handleApiSourceBridge(AsyncWebServerRequest *request,
                                  uint8_t *data, size_t len,
                                  size_t index, size_t total) {
  const char *body = nullptr; size_t bodyLen = 0;
  if (!accumulateReqBody(request, data, len, index, total, 512,
                         "{\"ok\":false,\"error\":\"payload too large\"}",
                         body, bodyLen)) return;
  DynamicJsonDocument in(256);
  if (deserializeJson(in, body, bodyLen) || !in["source"].is<const char*>()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  String s = in["source"].as<String>(); s.toUpperCase();
  if (!(s == "BT" || s == "AUX" || s == "USB")) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid source\"}");
    return;
  }
  if (s == "USB") {
    startServerUsbAudio();
    serverInputSource = SRC_FROM_USB;
  } else {
    stopServerUsbAudio();
    serverInputSource = SRC_FROM_LOCAL;
  }
  String resp; uint16_t code = 0;
  bool ok = bridgeSendReceiverCmd(String("SRC:") + s, resp, code);
  if (ok) { lastReceiverSource = s; request->send(200, "application/json", resp); }
  else    request->send(502, "application/json", resp);
}

inline void handleApiCmdBridge(AsyncWebServerRequest *request,
                               uint8_t *data, size_t len,
                               size_t index, size_t total) {
  const char *body = nullptr; size_t bodyLen = 0;
  if (!accumulateReqBody(request, data, len, index, total, 512,
                         "{\"ok\":false,\"error\":\"payload too large\"}",
                         body, bodyLen)) return;
  DynamicJsonDocument in(256);
  if (deserializeJson(in, body, bodyLen) || !in["cmd"].is<const char*>()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  String cmd = in["cmd"].as<String>();
  cmd.toLowerCase();
  if (!(cmd == "next" || cmd == "prev" || cmd == "bt" || cmd == "aux" || cmd == "usb")) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid cmd\"}");
    return;
  }
  cmd.toUpperCase();
  String resp; uint16_t code = 0;
  if (bridgeSendReceiverCmd(cmd, resp, code)) request->send(200, "application/json", resp);
  else request->send(502, "application/json", resp);
}

inline void handleApiBtDevices(AsyncWebServerRequest *request) {
  String cached = "{\"devices\":[]}";
  if (lockFor(uartMutex, 20)) { cached = receiverBtJson; unlockFor(uartMutex); }
  sendJson(request, cached);
}

inline void handleApiBtKick(AsyncWebServerRequest *request) {
  String resp; uint16_t code = 0;
  if (bridgeSendReceiverCmd("KICK_BT", resp, code)) request->send(200, "application/json", resp);
  else request->send(502, "application/json", resp);
}

// ---- /api/config (relayed: volume/mute/mode/buffer belong to the stream) --
inline void handleApiConfigRelay(AsyncWebServerRequest *request,
                                 uint8_t *data, size_t len,
                                 size_t index, size_t total) {
  const char *body = nullptr; size_t bodyLen = 0;
  if (!accumulateReqBody(request, data, len, index, total, 1024,
                         "{\"ok\":false,\"error\":\"payload too large\"}",
                         body, bodyLen)) return;
  String b(body ? body : "");

  // Mirror the operator's intent locally for the parts CTR owns, so the
  // capture side stays coherent even when STR is offline. Only the fields
  // that exist on both boards are interpreted here.
  DynamicJsonDocument in(512);
  if (!deserializeJson(in, b)) {
    if (in["masterVolume"].is<int>()) {
      int v = in["masterVolume"];
      if (v >= 0 && v <= 100) { masterVolume = (uint8_t)v; if (!in.containsKey("masterMuted")) masterMuted = false; }
    }
    if (in["masterMuted"].is<bool>()) masterMuted = in["masterMuted"].as<bool>();
    if (in["mode"].is<const char*>()) {
      String m = in["mode"].as<String>(); m.toLowerCase();
      ModeIdx oldM = (ModeIdx)playbackModeIdx;
      ModeIdx newM = (m == "off") ? MODE_OFF : (m == "movie") ? MODE_MOVIE : MODE_MUSIC;
      if (newM != oldM) {
        captureLiveIntoMode(oldM);
        playbackModeIdx = (uint8_t)newM;
        applyModeSettingsToLive(newM);
      }
    }
    if (in["quietMode"].is<bool>()) {
      quietModeEnabled = in["quietMode"].as<bool>();
      g_quietFiltersNeedReconfig = true;
    }
    if (in["audioBufferMs"].is<int>()) {
      int bm = in["audioBufferMs"];
      if (bm >= 20 && bm <= 600) audioBufferMs = (uint16_t)bm;
    }
  }
  settingsDirty      = true;
  settingsDirtySince = millis();

  String out; int code = 0;
  bool ok = downlinkRequest("POST", "/api/config", b, out, code);
  if (!ok) {
    // STR offline: the capture side has already applied what it can, so this
    // is a partial success, reported honestly.
    request->send(200, "application/json",
                  "{\"ok\":true,\"downlink\":false,\"applied\":\"ctr-only\"}");
    return;
  }
  request->send(code, "application/json", out);
}

// ---- setup -----------------------------------------------------------------
inline void setup_http_server() {
  // ---- dashboard ----
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *res = request->beginResponse_P(
        200, "text/html", EVDCTRL_GZ, EVDCTRL_GZ_LEN);
    res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "no-store, max-age=0");
    request->send(res);
  });

  // ---- network setup ----
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", networkSettingsPage().c_str());
  });
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Body-less form post: the dashboard posts JSON to /api/network/save.
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"use JSON\"}");
  },
  nullptr,
  [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
     size_t index, size_t total) {
    handleSaveNetworkSettings(request, data, len, index, total);
  });
  server.on("/api/network/state", HTTP_GET, handleNetworkState);
  server.on("/api/network-test",  HTTP_GET, handleNetworkTestStatus);
  server.on("/api/wifi/scan/start",  HTTP_GET, handleWifiScanStart);
  server.on("/api/wifi/scan/status", HTTP_GET, handleWifiScanStatus);
  server.on("/api/restart", HTTP_POST, handleRestart);

  // ---- state / stats / log ----
  server.on("/api/state", HTTP_GET, handleApiState);
  server.on("/api/stats", HTTP_GET, handleApiStats);
  server.on("/api/log/tail", HTTP_GET, handleLogTail);
  server.on("/api/log/level", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendJson(req, String("{\"level\":") + String((int)gSerialLogLevel) + "}");
  });
  server.on("/api/log/level", HTTP_POST,
    [](AsyncWebServerRequest *) {},
    nullptr,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len,
       size_t index, size_t total) {
      const char *body = nullptr; size_t bodyLen = 0;
      if (!accumulateReqBody(req, data, len, index, total, 128,
                             "{\"ok\":false}", body, bodyLen)) return;
      DynamicJsonDocument in(64);
      if (!deserializeJson(in, body, bodyLen) && in["level"].is<int>()) {
        int l = in["level"].as<int>();
        if (l >= 0 && l <= 3) gSerialLogLevel = (uint8_t)l;
      }
      req->send(200, "application/json", "{\"ok\":true}");
    });

  // ---- downlink (EVDSTR) ----
  server.on("/api/link/state",   HTTP_GET,  handleLinkState);
  server.on("/api/link/pair",    HTTP_POST, [](AsyncWebServerRequest *) {}, nullptr, handleLinkPair);
  server.on("/api/link/players", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendJson(req, downlinkPlayersJson());
  });
  server.on("/api/link/zones",   HTTP_GET, [](AsyncWebServerRequest *req) {
    sendJson(req, downlinkZonesJson());
  });

  // ---- encoder (local) ----
  server.on("/api/audio/encoding",          HTTP_GET,  handleGetAudioEncoding);
  server.on("/api/audio/encoding",          HTTP_POST, [](AsyncWebServerRequest *) {}, nullptr, handlePostAudioEncoding);
  server.on("/api/audio/encoding/revert",   HTTP_POST, handlePostAudioEncodingRevert);
  server.on("/api/audio/encoding/defaults", HTTP_POST, handlePostAudioEncodingDefaults);

  // ---- relayed to EVDSTR ----
  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *) {}, nullptr, handleApiConfigRelay);
  server.on("/api/zones/create",      HTTP_POST, [](AsyncWebServerRequest *) {}, nullptr, DOWNLINK_FORWARD_CTR("/api/zones/create"));
  server.on("/api/zones/delete",      HTTP_POST, [](AsyncWebServerRequest *) {}, nullptr, DOWNLINK_FORWARD_CTR("/api/zones/delete"));
  server.on("/api/zones/move",        HTTP_POST, [](AsyncWebServerRequest *) {}, nullptr, DOWNLINK_FORWARD_CTR("/api/zones/move"));
  server.on("/api/zones/config",      HTTP_POST, [](AsyncWebServerRequest *) {}, nullptr, DOWNLINK_FORWARD_CTR("/api/zones/config"));
  server.on("/api/zones/remove-node", HTTP_POST, [](AsyncWebServerRequest *) {}, nullptr, DOWNLINK_FORWARD_CTR("/api/zones/remove-node"));
  server.on("/api/player/register",   HTTP_POST, [](AsyncWebServerRequest *) {}, nullptr, DOWNLINK_FORWARD_CTR("/api/player/register"));
  server.on("/api/node_stats",        HTTP_POST, [](AsyncWebServerRequest *) {}, nullptr, DOWNLINK_FORWARD_CTR("/api/node_stats"));
  server.on("/api/player/ota_result", HTTP_POST, [](AsyncWebServerRequest *) {}, nullptr, DOWNLINK_FORWARD_CTR("/api/player/ota_result"));

  // ---- source board ----
  server.on("/api/source",     HTTP_POST, [](AsyncWebServerRequest *) {}, nullptr, handleApiSourceBridge);
  server.on("/api/cmd",        HTTP_POST, [](AsyncWebServerRequest *) {}, nullptr, handleApiCmdBridge);
  server.on("/api/bt/devices", HTTP_GET,  handleApiBtDevices);
  server.on("/api/bt/kick",    HTTP_POST, handleApiBtKick);
  server.on("/api/discover",   HTTP_GET, [](AsyncWebServerRequest *req) {
    sendJson(req, String("{\"role\":\"ctr\",\"name\":\"") + jsonEscape(serverName) +
                   "\",\"ip\":\"" + WiFi.localIP().toString() + "\"}");
  });

  // ---- DLNA ----
  server.on("/description.xml",   HTTP_GET, dlnaHandleDescription);
  server.on("/upnp/scpd/avt.xml", HTTP_GET, [](AsyncWebServerRequest *r) { r->send_P(200, "text/xml", DLNA_SCPD_AVT); });
  server.on("/upnp/scpd/rcs.xml", HTTP_GET, [](AsyncWebServerRequest *r) { r->send_P(200, "text/xml", DLNA_SCPD_RCS); });
  server.on("/upnp/scpd/cms.xml", HTTP_GET, [](AsyncWebServerRequest *r) { r->send_P(200, "text/xml", DLNA_SCPD_CMS); });
  server.on("/upnp/control/avt", HTTP_POST,
    [](AsyncWebServerRequest *r) {},
    nullptr,
    [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
      const char *body = nullptr; size_t bodyLen = 0;
      if (!accumulateReqBody(r, data, len, index, total, 4096,
                             "<error/>", body, bodyLen)) return;
      dlnaHandleSoap(r, String(body), "avt");
    });
  server.on("/upnp/control/rcs", HTTP_POST,
    [](AsyncWebServerRequest *r) {},
    nullptr,
    [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
      const char *body = nullptr; size_t bodyLen = 0;
      if (!accumulateReqBody(r, data, len, index, total, 4096,
                             "<error/>", body, bodyLen)) return;
      dlnaHandleSoap(r, String(body), "rcs");
    });
  server.on("/upnp/control/cms", HTTP_POST,
    [](AsyncWebServerRequest *r) {},
    nullptr,
    [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
      const char *body = nullptr; size_t bodyLen = 0;
      if (!accumulateReqBody(r, data, len, index, total, 4096,
                             "<error/>", body, bodyLen)) return;
      dlnaHandleSoap(r, String(body), "cms");
    });
  server.on("/upnp/event/avt", HTTP_ANY, [](AsyncWebServerRequest *r) { r->send(200, "text/xml", ""); });
  server.on("/upnp/event/rcs", HTTP_ANY, [](AsyncWebServerRequest *r) { r->send(200, "text/xml", ""); });
  server.on("/upnp/event/cms", HTTP_ANY, [](AsyncWebServerRequest *r) { r->send(200, "text/xml", ""); });

  // ---- OTA (CTR self-update + STR push) ----
  setupOtaRoutes();

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
  });

  server.begin();
  LOGI("HTTP server started on :80");
}
