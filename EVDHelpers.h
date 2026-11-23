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
// AsyncWebServer delivers a POST body in segments, and several requests can be
// in flight at once on the same AsyncTCP worker: a player's
// POST /api/player/register every few seconds lands on the same worker as a
// dashboard POST /api/config. A single `static String` here was therefore a
// race: request B's first segment reset the buffer while request A was still
// being accumulated, and A's handler parsed B's bytes.
//
// The buffer now lives on the request object (`request->_tempObject`), which
// AsyncWebServer frees when the request is destroyed. It is never shared, so
// overlapping requests cannot corrupt each other.
//
// Ownership: if the function returns true, the CALLER owns the buffer and must
// release it with releaseReqBody(request). If it returns false the buffer has
// already been released (and an error response sent, or a partial body
// abandoned).
inline bool accumulateReqBody(AsyncWebServerRequest *request,
                              const uint8_t *data, size_t len, size_t index,
                              size_t total, size_t cap,
                              const char *tooLargeJson,
                              const char *&bodyOut, size_t &lenOut) {
  String *slot = (String *)request->_tempObject;
  if (index == 0) {
    if (total > cap) {
      request->send(413, "application/json", tooLargeJson);
      return false;
    }
    if (slot) { delete slot; request->_tempObject = nullptr; }
    slot = new (std::nothrow) String();
    if (!slot) {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"no memory\"}");
      return false;
    }
    request->_tempObject = slot;
    slot->reserve(total + 1);
  } else if (!slot) {
    // A continuation with no head: the first segment was dropped (413) or this
    // is a stray tail. Nothing to append to.
    return false;
  }

  if (slot->length() + len > cap) {
    request->_tempObject = nullptr;
    delete slot;
    request->send(413, "application/json", tooLargeJson);
    return false;
  }

  // Bulk append: String::concat(const char*, n) copies n bytes in one pass.
  slot->concat((const char *)data, len);

  if (index + len >= total) {
    bodyOut = slot->c_str();
    lenOut  = slot->length();
    return true;
  }
  return false;
}

// Hand the accumulated body back. Call once the handler has finished reading
// bodyOut; nothing may touch bodyOut after this.
inline void releaseReqBody(AsyncWebServerRequest *request) {
  String *slot = (String *)request->_tempObject;
  if (!slot) return;
  request->_tempObject = nullptr;
  delete slot;
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
