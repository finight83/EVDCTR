#pragma once
// ============================================================================
//  EVDHelpers.h - small shared utilities for EVDCTR
// ============================================================================

// ---- NVS guard -------------------------------------------------------------
// Every Preferences open()/close() pair goes through this so two cores can
// never hold the NVS namespace at the same time. Keep the critical section
// short: NVS writes stall the flash cache for ~100 ms on this chip.
struct PrefsGuard {
  Preferences prefs;
  bool        ok = false;
  explicit PrefsGuard(const char *ns, bool ro = false) {
    if (prefsMutex) xSemaphoreTake(prefsMutex, portMAX_DELAY);
    ok = prefs.begin(ns, ro);
  }
  ~PrefsGuard() {
    if (ok) prefs.end();
    if (prefsMutex) xSemaphoreGive(prefsMutex);
  }
  PrefsGuard(const PrefsGuard &) = delete;
  PrefsGuard &operator=(const PrefsGuard &) = delete;
};

inline bool lockFor(SemaphoreHandle_t m, uint32_t ms) {
  if (!m) return false;
  return xSemaphoreTake(m, pdMS_TO_TICKS(ms)) == pdTRUE;
}
inline void unlockFor(SemaphoreHandle_t m) {
  if (m) xSemaphoreGive(m);
}

// ---- Escaping --------------------------------------------------------------
// Player and zone names are rendered into the dashboard with innerHTML, so
// anything that came from the network must be escaped on the way out.
inline String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    unsigned char c = (unsigned char)in[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += (char)c;
        }
    }
  }
  return out;
}

inline String htmlEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    switch (c) {
      case '<': out += "&lt;";  break;
      case '>': out += "&gt;";  break;
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;";break;
      case '\'':out += "&#39;"; break;
      default:  out += c;
    }
  }
  return out;
}

// Server names become mDNS hostnames: letters and digits only, 1-32 chars.
inline String sanitizeServerName(const String &in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length() && out.length() < 32; ++i) {
    unsigned char c = (unsigned char)in[i];
    if (isAlphaNumeric(c)) out += (char)c;
  }
  return out;
}

// Version strings from a node/peer: digits and dots only, 16 chars.
inline String sanitizeVersion(const String &in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length() && out.length() < 16; ++i) {
    char c = in[i];
    if ((c >= '0' && c <= '9') || c == '.') out += c;
  }
  return out;
}

// ---- Request-body accumulation --------------------------------------------
// AsyncWebServer delivers a POST body in segments. Overlapping requests must
// not share a buffer, so the accumulation is per-request: the first segment
// allocates, later ones append, the handler frees. `slot` is the caller's
// static String, which is safe because the AsyncTCP worker is single-threaded
// and one request's segments are serialised.
inline bool accumulateReqBody(AsyncWebServerRequest *request,
                              const uint8_t *data, size_t len, size_t index,
                              size_t total, size_t cap,
                              const char *tooLargeJson,
                              const char *&bodyOut, size_t &lenOut) {
  static String slot;
  if (index == 0) {
    slot = "";
    if (total > cap) {
      request->send(413, "application/json", tooLargeJson);
      return false;
    }
    slot.reserve(total + 1);
  }
  if (slot.length() + len > cap) {
    slot = "";
    request->send(413, "application/json", tooLargeJson);
    return false;
  }
  for (size_t i = 0; i < len; ++i) slot += (char)data[i];
  if (index + len >= total) {
    bodyOut = slot.c_str();
    lenOut  = slot.length();
    return true;
  }
  return false;
}

// ---- Log ring writer -------------------------------------------------------
#include <cstdarg>
inline void evdLogPrintf(char level, const char *fmt, ...) {
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  portENTER_CRITICAL(&gLogRingMux);
    LogRingEntry &e = gLogRing[gLogRingSeq % EVD_LOG_RING_LEN];
    e.seq   = ++gLogRingSeq;
    e.ms    = millis();
    e.level = level;
    strncpy(e.text, buf, sizeof(e.text) - 1);
    e.text[sizeof(e.text) - 1] = '\0';
  portEXIT_CRITICAL(&gLogRingMux);

  if (level == 'E') { Serial.printf("[E] %s\n", buf); return; }
  if (level == 'W') { Serial.printf("[W] %s\n", buf); return; }
  Serial.printf("[I] %s\n", buf);
}
