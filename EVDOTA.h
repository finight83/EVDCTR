#pragma once
// ============================================================================
//  EVDOTA.h - EVDCTR firmware update
// ============================================================================
//
//  Two targets, one spool:
//
//    ctr  - this board. The uploaded .bin is written to the spare OTA
//           partition with Update.h and the board restarts.
//    str  - EVDSTR. CTR has the LittleFS spool and the dashboard, so an STR
//           image is uploaded here and pushed over WiFi to STR's own
//           /api/ota/upload. The link pins carry audio, not firmware; a
//           2 MB image over a 1 Mbit pin link would take 20 s and stall the
//           encoder for all of it.
//
//  Upload, spool, apply and delete are all on the AsyncTCP worker (core 0).
//  Audio keeps running during the upload; it is paused only for the brief
//  flash write of the apply step, which is what otaPauseAudio is for.
// ============================================================================
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include <esp_heap_caps.h>
#include <LittleFS.h>
#include <HTTPClient.h>

volatile bool otaUploadInProgress = false;
volatile bool otaInProgress       = false;
bool          otaUploadFailed     = false;
String        otaUploadMessage    = "";

#define OTA_SPOOL_DIR  "/ota"
#define OTA_SPOOL_CTR  OTA_SPOOL_DIR "/evdctr.bin"
#define OTA_SPOOL_STR  OTA_SPOOL_DIR "/evdstr.bin"

inline void otaEnsureSpoolDir() {
  if (!LittleFS.exists(OTA_SPOOL_DIR)) LittleFS.mkdir(OTA_SPOOL_DIR);
}

// ---- status ----------------------------------------------------------------
inline void handleOtaStatus(AsyncWebServerRequest *request) {
  bool haveCtr = LittleFS.exists(OTA_SPOOL_CTR);
  bool haveStr = LittleFS.exists(OTA_SPOOL_STR);
  String j = String("{\"ok\":true,\"uploading\":") + (otaUploadInProgress ? "true" : "false") +
             ",\"applying\":" + (otaInProgress ? "true" : "false") +
             ",\"message\":\"" + jsonEscape(otaUploadMessage) + "\"" +
             ",\"ctr\":{\"spooled\":" + (haveCtr ? "true" : "false") +
             ",\"bytes\":" + String(haveCtr ? (unsigned long)LittleFS.open(OTA_SPOOL_CTR, "r").size() : 0UL) + "}" +
             ",\"str\":{\"spooled\":" + (haveStr ? "true" : "false") +
             ",\"bytes\":" + String(haveStr ? (unsigned long)LittleFS.open(OTA_SPOOL_STR, "r").size() : 0UL) + "}" +
             ",\"version\":\"" EVD_FW_VERSION "\"}";
  // sendJson() lives in EVDRoutes.h, which comes after this file: send it
  // directly rather than reaching forward.
  AsyncWebServerResponse *res = request->beginResponse(200, "application/json", j);
  res->addHeader("Cache-Control", "no-store, max-age=0");
  request->send(res);
}

// ---- upload ----------------------------------------------------------------
// One endpoint, ?target=ctr|str. Writes straight to the spool file; nothing is
// flashed until the operator presses Apply.
inline void handleOtaUpload(AsyncWebServerRequest *request, String filename,
                            size_t index, uint8_t *data, size_t len,
                            bool final) {
  static File   f;
  static bool   failed = false;
  static String target = "ctr";

  if (index == 0) {
    failed = false;
    otaUploadFailed = false;
    target = request->hasParam("target") ? request->getParam("target")->value() : "ctr";
    if (target != "ctr" && target != "str") target = "ctr";
    otaEnsureSpoolDir();
    const char *path = (target == "str") ? OTA_SPOOL_STR : OTA_SPOOL_CTR;
    if (LittleFS.exists(path)) LittleFS.remove(path);
    f = LittleFS.open(path, "w");
    if (!f) {
      failed = true;
      otaUploadFailed = true;
      otaUploadMessage = "cannot open spool file";
      return;
    }
    otaUploadInProgress = true;
    otaUploadMessage    = "receiving " + target + " image";
    LOGI("OTA: receiving %s image (%s)", target.c_str(), filename.c_str());
  }

  if (!failed && len && f) {
    if (f.write(data, len) != len) {
      failed = true;
      otaUploadFailed = true;
      otaUploadMessage = "write failed (flash full?)";
      LOGE("OTA: spool write failed at %u", (unsigned)index);
    }
  }

  if (final) {
    if (f) f.close();
    otaUploadInProgress = false;
    if (!failed) {
      otaUploadMessage = target + " image spooled";
      LOGI("OTA: %s image spooled", target.c_str());
    }
  }
}

// ---- apply -----------------------------------------------------------------
// CTR: Update.h writes the spare partition and we restart.
// STR: HTTP multipart POST of the spooled file to STR's /api/ota/upload, then
//      POST /api/ota/apply.
static bool otaPushSpoolToStr() {
  if (!strHost.length()) { otaUploadMessage = "no EVDSTR paired"; return false; }
  // Second gate: the host is validated when it is paired, but a firmware POST
  // is destructive enough to check again here rather than trust the stored
  // value.
  if (!isPairableHost(strHost)) {
    otaUploadMessage = "paired STR address is not valid";
    return false;
  }
  File f = LittleFS.open(OTA_SPOOL_STR, "r");
  if (!f) { otaUploadMessage = "no STR image spooled"; return false; }
  const size_t size = f.size();
  if (size == 0) { f.close(); otaUploadMessage = "spooled STR image is empty"; return false; }

  String boundary = "----EVDCTR" + String((unsigned long)millis());
  String head = "--" + boundary + "\r\n"
                "Content-Disposition: form-data; name=\"update\"; filename=\"evdstr.bin\"\r\n"
                "Content-Type: application/octet-stream\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";
  const size_t total = head.length() + size + tail.length();

  // One request, one buffer. HTTPClient has no streaming multipart API, so
  // the body is assembled in PSRAM: a 2 MB image plus its ~200-byte envelope
  // does not fit in internal DRAM next to the HTTP and audio stacks.
  uint8_t *buf = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
  if (!buf) { f.close(); otaUploadMessage = "PSRAM alloc failed"; return false; }
  memcpy(buf, head.c_str(), head.length());
  size_t got = f.read(buf + head.length(), size);
  f.close();
  if (got != size) {
    heap_caps_free(buf);
    otaUploadMessage = "short read from the spool";
    return false;
  }
  memcpy(buf + head.length() + size, tail.c_str(), tail.length());

  bool ok = false;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(120000);            // a 2 MB POST over WiFi takes a while
  if (http.begin(String("http://") + strHost + "/api/ota/upload")) {
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    int c = http.POST(buf, total);
    ok = (c == 200);
    otaUploadMessage = ok ? "EVDSTR accepted the image"
                          : ("EVDSTR upload returned " + String(c));
    http.end();
  } else {
    otaUploadMessage = "cannot reach EVDSTR";
  }
  heap_caps_free(buf);
  if (!ok) return false;

  // STR restarts itself inside its upload handler once Update.end() succeeds,
  // so /api/ota/apply is a courtesy ack, not a requirement. Its reply may
  // never arrive because the board is already rebooting.
  String out; int c2 = 0;
  downlinkRequest("POST", "/api/ota/apply", "{\"target\":\"str\"}", out, c2);
  otaUploadMessage = "EVDSTR update pushed, board restarting";
  return true;
}

inline void handleOtaApply(AsyncWebServerRequest *request) {
  String target = request->hasParam("target") ? request->getParam("target")->value() : "ctr";
  if (target == "str") {
    bool ok = otaPushSpoolToStr();
    request->send(ok ? 200 : 502, "application/json",
                  String("{\"ok\":") + (ok ? "true" : "false") +
                  ",\"message\":\"" + jsonEscape(otaUploadMessage) + "\"}");
    return;
  }

  File f = LittleFS.open(OTA_SPOOL_CTR, "r");
  if (!f) {
    request->send(404, "application/json",
                  "{\"ok\":false,\"error\":\"no CTR image spooled\"}");
    return;
  }
  size_t size = f.size();
  otaInProgress  = true;
  otaPauseAudio  = true;
  request->send(200, "application/json", "{\"ok\":true,\"restart\":true}");

  if (!Update.begin(size)) {
    LOGE("OTA: Update.begin failed (%s)", Update.errorString());
    otaInProgress = false;
    otaPauseAudio = false;
    f.close();
    return;
  }
  size_t written = Update.writeStream(f);
  f.close();
  if (written != size) {
    LOGE("OTA: wrote %u of %u bytes (%s)", (unsigned)written, (unsigned)size,
         Update.errorString());
    Update.abort();
    otaInProgress = false;
    otaPauseAudio = false;
    return;
  }
  if (!Update.end(true)) {
    LOGE("OTA: Update.end failed (%s)", Update.errorString());
    otaInProgress = false;
    otaPauseAudio = false;
    return;
  }
  LOGI("OTA: CTR image applied, restarting");
  delay(300);
  ESP.restart();
}

inline void handleOtaDelete(AsyncWebServerRequest *request) {
  String target = request->hasParam("target") ? request->getParam("target")->value() : "ctr";
  const char *path = (target == "str") ? OTA_SPOOL_STR : OTA_SPOOL_CTR;
  bool removed = LittleFS.exists(path) ? LittleFS.remove(path) : false;
  request->send(200, "application/json",
                String("{\"ok\":true,\"removed\":") + (removed ? "true" : "false") + "}");
}

// The server instance lives in EVDRoutes.h, which is included after this file.
extern AsyncWebServer server;

inline void setupOtaRoutes() {
  otaEnsureSpoolDir();
  server.on("/api/ota/status", HTTP_GET, handleOtaStatus);
  server.on("/api/ota/upload", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      bool ok = !otaUploadFailed;
      request->send(ok ? 200 : 500, "application/json",
                    String("{\"ok\":") + (ok ? "true" : "false") +
                    ",\"message\":\"" + jsonEscape(otaUploadMessage) + "\"}");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index,
       uint8_t *data, size_t len, bool final) {
      handleOtaUpload(request, filename, index, data, len, final);
    });
  server.on("/api/ota/apply",  HTTP_POST, handleOtaApply);
  server.on("/api/ota/delete", HTTP_POST, handleOtaDelete);
  LOGI("OTA: routes ready (CTR self-update + EVDSTR push)");
}
