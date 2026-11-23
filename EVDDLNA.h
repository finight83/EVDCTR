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
volatile bool dlnaFetchRunning = false;
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
      uint32_t skip = 44;   // canonical WAV header
      bool     headerDone = false;

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

        int off = 0;
        if (!headerDone) {
          if ((uint32_t)got <= skip) { skip -= (uint32_t)got; continue; }
          off = (int)skip;
          skip = 0;
          headerDone = true;
        }
        int payload = got - off;
        if (payload <= 0) continue;
        payload &= ~3;   // whole stereo 16-bit frames only

        chunk.len         = (uint32_t)payload;
        chunk.captured_ms = millis();
        memcpy(chunk.data, buf + off, payload);
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
               String(service == "avt" ? "AVTransport" :
                      service == "rcs" ? "RenderingControl" : "ConnectionManager") +
               ":1\">" + result + "</u:Response></s:Body></s:Envelope>";
  request->send(200, "text/xml; charset=\"utf-8\"", out);
}

inline void initDlna() {
  BaseType_t rc = xTaskCreatePinnedToCore(dlnaSsdpTaskFn, "dlnaSsdp", 4096,
                                          nullptr, 2, nullptr, 0);
  if (rc != pdPASS) LOGE("DLNA: SSDP task create failed (%d)", (int)rc);
  else LOGI("DLNA: MediaRenderer ready (fetch task pinned to core 0)");
}
