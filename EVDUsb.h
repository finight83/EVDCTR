#pragma once
// ============================================================================
//  EVDUsb.h - USB Audio Class (UAC1) capture for EVDCTR
// ============================================================================
//
//  The box enumerates as a 48 kHz / 16-bit / stereo *microphone* so a PC or
//  phone can send it audio as a normal Windows sound device. Same role as on
//  the unsplit server: this is the "USB" input the dashboard's source
//  selector picks, and it feeds rawQueue like every other input.
//
//  The volume/mute mirroring is kept: Windows polls the UAC1 feature unit on
//  connect and pushes slider changes, and it is genuinely useful to see on
//  the dashboard whether the host has muted us.
// ============================================================================
#include <esp_heap_caps.h>

// The USB device stack is an optional library, and this file is currently a
// STUB: it calls setVid()/begin()/read()/connected() on EspUsbDevice, which is
// not the API the library actually has. The unsplit server used
// EspUsbAudioFunction + addPlaybackStream() + addFormat({rate, ch, 2, 16}) +
// begin(EspUsbDeviceConfig{...}); see srv_EVDUsb.h in the original repo.
//
// Until that block is ported across, USB is compiled out rather than shipped
// as something that looks like it works. Set EVD_HAS_USB_DEVICE_LIB to 1 in
// EVDCTR.ino once the port is done.
#if EVD_HAS_USB_DEVICE_LIB
#if __has_include(<EspUsbDevice.h>)
#include <EspUsbDevice.h>
#else
#undef EVD_HAS_USB_DEVICE_LIB
#define EVD_HAS_USB_DEVICE_LIB 0
#pragma message("EVDCTR: EspUsbDevice.h not found, USB input disabled")
#endif
#else
#pragma message("EVDCTR: USB input not ported yet (see EVDUsb.h), compiled out")
#endif

volatile int32_t gUsbHostVolumeDb256 = 0;
volatile bool    gUsbHostMuted       = false;
volatile bool    gUsbStreaming       = false;

#if EVD_HAS_USB_DEVICE_LIB
static EspUsbDevice gUsbDev;
static uint8_t     *gUsbRxBuf = nullptr;
static const size_t USB_RX_BUF_BYTES = 4096;

// UAC1 speaker/microphone descriptor: 48 kHz, 16-bit, 2ch, with a feature
// unit so the host shows a volume slider.
static const uint8_t USB_UAC1_DESC[] = {
  0x05, 0x0c,        // USAGE_PAGE (Consumer)
  0x09, 0x01,        // USAGE (Consumer Control)
  0xa1, 0x01,        // COLLECTION (Application)
  0x85, 0x01,        //   REPORT_ID (1)
  0x09, 0xe9,        //   USAGE (Volume Increment)
  0x09, 0xea,        //   USAGE (Volume Decrement)
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02,
  0xc0
};

inline void startServerUsbAudio() {
  if (gUsbRxBuf) return;
  gUsbRxBuf = (uint8_t *)heap_caps_malloc(USB_RX_BUF_BYTES, MALLOC_CAP_8BIT);
  if (!gUsbRxBuf) { LOGE("USB: rx buffer alloc failed"); return; }

  gUsbDev.setVid(EVD_USB_VID);
  gUsbDev.setPid(EVD_USB_PID);
  gUsbDev.setProduct("Enkelvoud CTR Audio");
  gUsbDev.setManufacturer("Enkelvoud");
  if (!gUsbDev.begin()) {
    LOGE("USB: device begin failed");
    heap_caps_free(gUsbRxBuf);
    gUsbRxBuf = nullptr;
    return;
  }
  LOGI("USB: UAC1 48k/16/2 enumerated as %04x:%04x", EVD_USB_VID, EVD_USB_PID);
}

inline void stopServerUsbAudio() {
  if (lockFor(gUsbLifecycleMutex, 500)) {
    gUsbDev.end();
    unlockFor(gUsbLifecycleMutex);
  }
  if (gUsbRxBuf) { heap_caps_free(gUsbRxBuf); gUsbRxBuf = nullptr; }
  gUsbStreaming = false;
}

// Drain whatever the host has sent. Called from audioReadTask (core 0) in USB
// mode; it pushes the same RawChunk the I2S path pushes.
inline size_t readServerUsbAudio(RawChunk &out) {
  out.len = 0;
  if (!gUsbRxBuf) return 0;
  size_t got = gUsbDev.read(gUsbRxBuf, USB_RX_BUF_BYTES);
  if (got == 0) return 0;
  size_t n = got > I2S_READ_CHUNK_BYTES ? I2S_READ_CHUNK_BYTES : got;
  n &= ~3;   // whole stereo 16-bit frames
  memcpy(out.data, gUsbRxBuf, n);
  out.len         = (uint32_t)n;
  out.captured_ms = millis();
  gUsbStreaming   = true;
  return n;
}

inline void pollUsbAudioEvents() {
  // The host's mute/volume live in the feature unit; the device library
  // reports them through its event callback, which updates these mirrors.
  static unsigned long last = 0;
  if (millis() - last < 500) return;
  last = millis();
  if (!gUsbDev.connected() && gUsbStreaming) gUsbStreaming = false;
}
#else
inline void startServerUsbAudio() {}
inline void stopServerUsbAudio() {}
inline size_t readServerUsbAudio(RawChunk &out) { out.len = 0; return 0; }
inline void pollUsbAudioEvents() {}
#endif
