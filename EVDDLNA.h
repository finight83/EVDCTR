#pragma once
// ============================================================================
//  EVDDLNA.h - DLNA / UPnP MediaRenderer input for EVDCTR
// ============================================================================
//
//  A DLNA controller (BubbleUPnP, Windows Media Player "Cast to device", a
//  NAS app) finds this box over SSDP, sets a WAV/MP3 URI and presses play.
//  The fetch task pulls that URI over HTTP and feeds rawQueue, which is the
//  same entry point the I2S and USB paths use — so a DLNA stream gets the
//  same resampling, quiet mode, volume, encoder and link as local audio.
//
//  Core: this file runs on CORE 0 only. SSDP and the fetch task are both
//  pinned there. On the unsplit server the fetch task sat on core 1 next to
//  the encoder; on this board core 1 is reserved for opus_encode(), so the
//  fetch moved. That is the single behavioural difference from the original
//  and it costs nothing: the fetch task blocks on a socket, it does not
//  compute.
//
//  Scope: the full UPnP action set (Seek, Next, Previous, playlist) is not
//  implemented. A renderer that only has to accept a URI, play, pause, stop
//  and report position is enough for every controller in practice, and
//  pretending to support the rest is how a renderer ends up in a state its
//  transport can never leave.
// ============================================================================
#include <WiFiUdp.h>

#define DLNA_SSDP_PORT 1900
#define DLNA_SSDP_ADDR "239.255.255.250"

WiFiUDP dlnaSsdpUdp;
bool dlnaFetchRunning = false;
String        dlnaCurrentUri   = "";
String        dlnaCurrentTitle = "";

static void dlnaSendReply(IPAddress to, uint16_t port, const String &payload) {
  dlnaSsdpUdp.beginPacket(to, port);
  dlnaSsdpUdp.write((const uint8_t *)payload.c_str(), payload.length());
  dlnaSsdpUdp.endPacket();
}

inline void dlnaStopFetch() {
  if (dlnaFetchRunning) {
    dlnaFetchRunning = false;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  dlnaPlaying = false;
  dlnaPaused  = false;
  if (serverInputSource == SRC_FROM_NETWORK) serverInputSource = dlnaPrevSource;
}

// ---- SSDP ------------------------------------------------------------------
inline void dlnaSsdpTaskFn(void *) {
  LOGI("DLNA SSDP task on core %d", xPortGetCoreID());
  IPAddress group;
  group.fromString(DLNA_SSDP_ADDR);
  dlnaSsdpUdp.begin(DLNA_SSDP_PORT);
  dlnaSsdpUdp.beginMulticast(group, DLNA_SSDP_PORT);

  unsigned long lastAnnounce = 0;
  for (;;) {
    int sz = dlnaSsdpUdp.parsePacket();
    if (sz > 0) {
      char buf[512];
      int n = dlnaSsdpUdp.read(buf, sizeof(buf) - 1);
      if (n > 0) {
        buf[n] = '\0';
        String req = String(buf);
        bool isSearch = req.startsWith("M-SEARCH");
        if (isSearch && req.indexOf("ssdp:discover") >= 0) {
          String host = sanitizeServerName(serverName);
          String st   = "urn:schemas-upnp-org:device:MediaRenderer:1";
          String loc  = String("http://") + WiFi.localIP().toString() + "/description.xml";
          String reply =
              "HTTP/1.1 200 OK\r\n"
              "CACHE-CONTROL: max-age=1800\r\n"
              "EXT:\r\n"
              "LOCATION: " + loc + "\r\n"
              "SERVER: EVDCTR/1 UPnP/1.0 DLNADOC/1.50\r\n"
              "ST: " + st + "\r\n"
              "USN: uuid:evdctr-" + host + "::" + st + "\r\n\r\n";
          dlnaSendReply(dlnaSsdpUdp.remoteIP(), dlnaSsdpUdp.remotePort(), reply);
        }
      }
    }
    // NOTIFY alive every 15 minutes, as a renderer must.
    if (millis() - lastAnnounce > 900000UL) {
      lastAnnounce = millis();
      String st  = "urn:schemas-upnp-org:device:MediaRenderer:1";
      String loc = String("http://") + WiFi.localIP().toString() + "/description.xml";
      String notify =
          "NOTIFY * HTTP/1.1\r\n"
          "HOST: " DLNA_SSDP_ADDR ":1900\r\n"
          "CACHE-CONTROL: max-age=1800\r\n"
          "NT: " + st + "\r\n"
          "NTS: ssdp:alive\r\n"
          "LOCATION: " + loc + "\r\n"
          "SERVER: EVDCTR/1 UPnP/1.0 DLNADOC/1.50\r\n\r\n";
      IPAddress g; g.fromString(DLNA_SSDP_ADDR);
      dlnaSendReply(g, DLNA_SSDP_PORT, notify);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ---- WAV header ------------------------------------------------------------
// Parses as much of the RIFF header as has arrived and returns the number of
// bytes to discard before the PCM. Sets g_sourceSampleRate so the stage-1
// resampler matches the file instead of assuming 48 kHz.
//
// Chunk walking, not a fixed 44: `fmt ` may be followed by LIST/fact/bext
// chunks before `data`, which is exactly what every encoder that writes
// metadata produces.
struct DlnaWav {
  uint32_t skip   = 0;
  uint16_t format = 0;
  uint16_t channels = 0;
  uint16_t bits   = 0;
  uint32_t rate   = 0;
  bool     ok     = false;
};
static DlnaWav dlnaWav;
static uint32_t dlnaWavSkip = 0;
static bool     dlnaWavMono = false;

inline bool parseWavHeader(const uint8_t *b, int n) {
  dlnaWav = DlnaWav();
  if (n < 12) return false;
  if (memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) return false;

  uint32_t pos = 12;
  bool haveFmt = false, haveData = false;
  while (pos + 8 <= (uint32_t)n) {
    uint32_t len = (uint32_t)b[pos + 4] | ((uint32_t)b[pos + 5] << 8) |
                   ((uint32_t)b[pos + 6] << 16) | ((uint32_t)b[pos + 7] << 24);
    if (memcmp(b + pos, "fmt ", 4) == 0) {
      // The 16 bytes that matter may straddle the end of this segment. Ask for
      // more instead of reporting "not a WAV".
      if (pos + 8 + 16 > (uint32_t)n) return false;
      dlnaWav.format   = (uint16_t)(b[pos + 8] | (b[pos + 9] << 8));
      dlnaWav.channels = (uint16_t)(b[pos + 10] | (b[pos + 11] << 8));
      dlnaWav.rate     = (uint32_t)b[pos + 12] | ((uint32_t)b[pos + 13] << 8) |
                         ((uint32_t)b[pos + 14] << 16) | ((uint32_t)b[pos + 15] << 24);
      dlnaWav.bits     = (uint16_t)(b[pos + 22] | (b[pos + 23] << 8));
      haveFmt = true;
    } else if (memcmp(b + pos, "data", 4) == 0) {
      if (!haveFmt) {
        // The data chunk precedes fmt (malformed, or fmt straddled a segment).
        // Refuse rather than guess a rate.
        return false;
      }
      dlnaWav.skip = pos + 8;
      haveData = true;
      break;
    }
    pos += 8 + len + (len & 1);   // chunks are word-aligned
    if (len > (uint32_t)n) break; // truncated header in this segment
  }
  if (!haveFmt || !haveData) return false;
  // Only 16-bit PCM. WAVE_FORMAT_EXTENSIBLE (0xFFFE) with 16 bits is PCM too.
  if (!(dlnaWav.format == 1 || dlnaWav.format == 0xFFFE)) return false;
  if (dlnaWav.bits != 16 || dlnaWav.channels < 1 || dlnaWav.channels > 2) return false;
  if (dlnaWav.rate < 8000 || dlnaWav.rate > 192000) return false;
  if (dlnaWav.rate != g_sourceSampleRate) {
    LOGI("DLNA: WAV %u Hz %u ch -> resampler", (unsigned)dlnaWav.rate,
         (unsigned)dlnaWav.channels);
    g_sourceSampleRate = dlnaWav.rate;
    g_rateEstimatorNeedsReset = true;
  }
  // Mono is widened to stereo by the fetch loop; that keeps the whole
  // downstream pipeline (which is hard-wired to CHANNELS = 2) unchanged.
  dlnaWavMono = (dlnaWav.channels == 1);
  dlnaWavSkip = dlnaWav.skip;
  dlnaWav.ok = true;
  return true;
}

// ---- Fetch -----------------------------------------------------------------
// Pulls the URI and pushes RawChunk into rawQueue at the natural chunk size.
// WAV: the 44-byte canonical header is skipped and the fmt block is parsed
// far enough to learn rate/channels; anything that is not 16-bit PCM is
// rejected loudly rather than streamed as noise.
inline void dlnaFetchTaskFn(void *) {
  LOGI("DLNA fetch task on core %d (uri %s)", xPortGetCoreID(),
       dlnaCurrentUri.c_str());
  dlnaFetchRunning = true;
  dlnaPlaying = true;
  dlnaPaused  = false;
  serverInputSource = SRC_FROM_NETWORK;

  HTTPClient http;
  http.setTimeout(5000);
  bool ok = http.begin(dlnaCurrentUri);
  if (!ok) { LOGE("DLNA: cannot open %s", dlnaCurrentUri.c_str()); }
  else {
    int code = http.GET();
    if (code != 200) {
      LOGE("DLNA: HTTP %d for %s", code, dlnaCurrentUri.c_str());
    } else {
      WiFiClient *stream = http.getStreamPtr();
      static uint8_t buf[I2S_READ_CHUNK_BYTES];
      RawChunk chunk;
      // The 44-byte "canonical" WAV header is a convention, not a guarantee:
      // encoders add LIST/INFO blocks before `data`, and the file may be
      // 8/24/32-bit or multichannel. Skipping exactly 44 bytes fed chunk
      // headers into the encoder as PCM (a burst of noise), and reading the
      // rate from the header is the only way the resampler can be right.
      bool     headerDone = false;
      uint32_t headerSkipRemaining = 0;

      while (dlnaFetchRunning && stream) {
        if (dlnaPaused) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        int avail = stream->available();
        if (avail <= 0) {
          if (!http.connected()) break;
          vTaskDelay(pdMS_TO_TICKS(5));
          continue;
        }
        int want = avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail;
        int got  = stream->read(buf, want);
        if (got <= 0) continue;

        // Finish discarding a header that straddled two segments.
        if (headerSkipRemaining) {
          uint32_t drop = (uint32_t)got < headerSkipRemaining
                          ? (uint32_t)got : headerSkipRemaining;
          headerSkipRemaining -= drop;
          got -= (int)drop;
          if (got <= 0) continue;
          memmove(buf, buf + drop, (size_t)got);
        }

        int off = 0;
        if (!headerDone) {
          if (got < 12) continue;             // need the RIFF/WAVE magic
          if (!parseWavHeader(buf, got)) {
            // Not a WAV we can play: stop loudly rather than stream garbage.
            LOGE("DLNA: %s is not 16-bit PCM WAV", dlnaCurrentUri.c_str());
            break;
          }
          if (dlnaWavSkip >= (uint32_t)got) {
            // The header is larger than this first segment. Skipping "44
            // bytes" unconditionally is what fed LIST-chunk headers into the
            // encoder as PCM; carry the remainder instead.
            headerSkipRemaining = dlnaWavSkip - (uint32_t)got;
            continue;
          }
          off = (int)dlnaWavSkip;
          headerDone = true;
        }
        int payload = got - off;
        if (payload <= 0) continue;
        // Whole 16-bit frames: 2 bytes mono, 4 bytes stereo.
        payload &= dlnaWavMono ? ~1 : ~3;
        int maxPayload = (int)sizeof(chunk.data);
        if (dlnaWavMono) maxPayload &= ~3;   // must stay an even frame count
        if (payload > maxPayload) payload = maxPayload;

        chunk.len         = (uint32_t)payload;
        chunk.captured_ms = millis();
        if (dlnaWavMono) {
          // Duplicate each sample into L and R: the pipeline is stereo-only.
          const int16_t *src = (const int16_t *)(buf + off);
          int16_t       *dst = (int16_t *)chunk.data;
          int frames = payload / 2;
          for (int i = 0; i < frames; ++i) { dst[i * 2] = src[i]; dst[i * 2 + 1] = src[i]; }
          chunk.len = (uint32_t)(frames * 4);
        } else {
          memcpy(chunk.data, buf + off, (size_t)payload);
        }
        if (xQueueSend(rawQueue, &chunk, pdMS_TO_TICKS(50)) != pdTRUE) {
          statRawDropped++;
        }
      }
      LOGI("DLNA: stream ended");
    }
    http.end();
  }

  dlnaPlaying = false;
  dlnaFetchRunning = false;
  if (serverInputSource == SRC_FROM_NETWORK) serverInputSource = dlnaPrevSource;
  vTaskDelete(nullptr);
}

inline void dlnaStartFetch(const String &uri) {
  if (dlnaFetchRunning) dlnaStopFetch();
  dlnaPrevSource = serverInputSource;
  dlnaCurrentUri = uri;
  if (xTaskCreatePinnedToCore(dlnaFetchTaskFn, "dlnaFetch", 8192, nullptr, 2,
                              nullptr, 0) != pdPASS) {
    LOGE("DLNA: fetch task create failed");
  }
}

// ---- SOAP ------------------------------------------------------------------
// Minimal AVTransport + RenderingControl + ConnectionManager. Enough for a
// controller to discover, set a URI, play, pause, stop and set volume.
inline void dlnaHandleDescription(AsyncWebServerRequest *request) {
  String host = sanitizeServerName(serverName);
  String xml =
    "<?xml version=\"1.0\"?>\n"
    "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
    " <specVersion><major>1</major><minor>0</minor></specVersion>\n"
    " <device>\n"
    "  <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>\n"
    "  <friendlyName>Enkelvoud " + host + "</friendlyName>\n"
    "  <manufacturer>Enkelvoud</manufacturer>\n"
    "  <modelName>EVDCTR</modelName>\n"
    "  <UDN>uuid:evdctr-" + host + "</UDN>\n"
    "  <serviceList>\n"
    "   <service><serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>"
    "<serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>"
    "<SCPDURL>/upnp/scpd/avt.xml</SCPDURL><controlURL>/upnp/control/avt</controlURL>"
    "<eventSubURL>/upnp/event/avt</eventSubURL></service>\n"
    "   <service><serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>"
    "<serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>"
    "<SCPDURL>/upnp/scpd/rcs.xml</SCPDURL><controlURL>/upnp/control/rcs</controlURL>"
    "<eventSubURL>/upnp/event/rcs</eventSubURL></service>\n"
    "   <service><serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>"
    "<serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>"
    "<SCPDURL>/upnp/scpd/cms.xml</SCPDURL><controlURL>/upnp/control/cms</controlURL>"
    "<eventSubURL>/upnp/event/cms</eventSubURL></service>\n"
    "  </serviceList>\n"
    " </device>\n"
    "</root>\n";
  request->send(200, "text/xml", xml);
}

static const char DLNA_SCPD_AVT[] PROGMEM =
  "<?xml version=\"1.0\"?><scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
  "<actionList>"
  "<action><name>SetAVTransportURI</name><argumentList>"
  "<argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>"
  "<argument><name>CurrentURI</name><direction>in</direction><relatedStateVariable>AVTransportURI</relatedStateVariable></argument>"
  "<argument><name>CurrentURIMetaData</name><direction>in</direction><relatedStateVariable>AVTransportURIMetaData</relatedStateVariable></argument>"
  "</argumentList></action>"
  "<action><name>Play</name><argumentList>"
  "<argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>"
  "<argument><name>Speed</name><direction>in</direction><relatedStateVariable>TransportPlaySpeed</relatedStateVariable></argument>"
  "</argumentList></action>"
  "<action><name>Pause</name></action>"
  "<action><name>Stop</name></action>"
  "</actionList>"
  "<serviceStateTable>"
  "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</dataType></stateVariable>"
  "<stateVariable sendEvents=\"no\"><name>AVTransportURI</name><dataType>string</dataType></stateVariable>"
  "<stateVariable sendEvents=\"no\"><name>AVTransportURIMetaData</name><dataType>string</dataType></stateVariable>"
  "<stateVariable sendEvents=\"no\"><name>TransportPlaySpeed</name><dataType>string</dataType></stateVariable>"
  "</serviceStateTable></scpd>";

static const char DLNA_SCPD_RCS[] PROGMEM =
  "<?xml version=\"1.0\"?><scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
  "<actionList>"
  "<action><name>GetVolume</name></action>"
  "<action><name>SetVolume</name></action>"
  "<action><name>GetMute</name></action>"
  "<action><name>SetMute</name></action>"
  "</actionList>"
  "<serviceStateTable>"
  "<stateVariable sendEvents=\"no\"><name>Volume</name><dataType>ui2</dataType></stateVariable>"
  "<stateVariable sendEvents=\"no\"><name>Mute</name><dataType>boolean</dataType></stateVariable>"
  "</serviceStateTable></scpd>";

static const char DLNA_SCPD_CMS[] PROGMEM =
  "<?xml version=\"1.0\"?><scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
  "<actionList>"
  "<action><name>GetProtocolInfo</name></action>"
  "<action><name>GetCurrentConnectionIDs</name></action>"
  "</actionList>"
  "<serviceStateTable>"
  "<stateVariable sendEvents=\"no\"><name>Sink</name><dataType>string</dataType></stateVariable>"
  "</serviceStateTable></scpd>";

static String dlnaTagValue(const String &soap, const char *tag) {
  String open  = String("<") + tag + ">";
  String close = String("</") + tag + ">";
  int a = soap.indexOf(open);
  if (a < 0) return "";
  a += open.length();
  int b = soap.indexOf(close, a);
  if (b < 0) return "";
  return soap.substring(a, b);
}

inline void dlnaHandleSoap(AsyncWebServerRequest *request, const String &soap,
                           const char *service) {
  String action = dlnaTagValue(soap, "u:Action");   // rarely present; fall back
  if (!action.length()) {
    int body = soap.indexOf("<s:Body>");
    if (body >= 0) {
      int a = soap.indexOf('<', body + 8);
      int b = soap.indexOf('>', a);
      if (a >= 0 && b > a) {
        String tag = soap.substring(a + 1, b);
        if (tag.endsWith(" xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"") ||
            tag.indexOf(' ') >= 0) tag = tag.substring(0, tag.indexOf(' '));
        if (tag.startsWith("u:")) tag = tag.substring(2);
        action = tag;
      }
    }
  }

  String result;
  if (strcmp(service, "avt") == 0) {
    if (action == "SetAVTransportURI") {
      String uri = dlnaTagValue(soap, "CurrentURI");
      if (uri.length()) { dlnaCurrentUri = uri; LOGI("DLNA: SetAVTransportURI %s", uri.c_str()); }
    } else if (action == "Play") {
      if (dlnaCurrentUri.length() && !dlnaFetchRunning) dlnaStartFetch(dlnaCurrentUri);
      else if (dlnaPaused) { dlnaPaused = false; dlnaPlaying = true; }
    } else if (action == "Pause") {
      dlnaPaused = true;
    } else if (action == "Stop") {
      dlnaStopFetch();
    }
  } else if (strcmp(service, "rcs") == 0) {
    if (action == "SetVolume") {
      int v = dlnaTagValue(soap, "DesiredVolume").toInt();
      if (v >= 0 && v <= 100) { dlnaVolume = v; masterVolume = (uint8_t)v; }
    } else if (action == "GetVolume") {
      result = "<CurrentVolume>" + String(dlnaVolume) + "</CurrentVolume>";
    }
  } else if (strcmp(service, "cms") == 0) {
    if (action == "GetProtocolInfo") {
      result = "<Source></Source><Sink>http-get:*:audio/wav:*,http-get:*:audio/L16:*</Sink>";
    }
  }

  String out = "<?xml version=\"1.0\"?>\n"
               "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
               "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
               "<s:Body><u:Response xmlns:u=\"urn:schemas-upnp-org:service:" +
               String(strcmp(service, "avt") == 0 ? "AVTransport" :
                      strcmp(service, "rcs") == 0 ? "RenderingControl"
                                                  : "ConnectionManager") +
               ":1\">" + result + "</u:Response></s:Body></s:Envelope>";
  request->send(200, "text/xml; charset=\"utf-8\"", out);
}

inline void initDlna() {
  BaseType_t rc = xTaskCreatePinnedToCore(dlnaSsdpTaskFn, "dlnaSsdp", 4096,
                                          nullptr, 2, nullptr, 0);
  if (rc != pdPASS) LOGE("DLNA: SSDP task create failed (%d)", (int)rc);
  else LOGI("DLNA: MediaRenderer ready (fetch task pinned to core 0)");
}
