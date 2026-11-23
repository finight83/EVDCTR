#pragma once
// ============================================================================
//  EVDLinkSpi.h - ESP32-S3 SPI2/HSPI master half of the EVDCTR -> EVDSTR link
// ============================================================================
//
//  Producer side. Called from ONE task (audioEncode, core 1). No mutex: the
//  SPI bus is owned by that task for the life of the firmware.
//
//  Why SPI2_HOST and not the Arduino SPI class: the Arduino SPI driver
//  installs an ISR, a queue and a mutex per bus, and every transfer costs a
//  task-notification round trip. At 50 frames/s with a 20 ms budget that is
//  affordable, but the ESP-IDF polling-transaction API below does the same
//  thing in one call with no queue and no dynamic allocation per frame,
//  which is what a core that must only run opus_encode() wants.
//
//  Wiring (CTR -> STR), plus a ground return that must be a real wire:
//
//    CTR GPIO11  SCLK  -> STR GPIO11
//    CTR GPIO12  MOSI  -> STR GPIO12
//    CTR GPIO13  IRQ   -> STR GPIO13   (CTR drives: "a frame is ready")
//    CTR GPIO14  CS    <- STR GPIO14   (STR drives: "clock me", active low)
//    GND               <-> GND         (mandatory, one per ~10 cm of ribbon)
//
//  SCLK/MOSI are the S3's native FSPI IOMUX pins, so they get the direct
//  path; IRQ and CS are ordinary GPIOs because they are handshakes, not
//  clocks, and do not need IOMUX.
// ============================================================================
#include <Arduino.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include "EVDLink.h"

// ---- Pin map (override in EVDCTRConfig.h if the board differs) --------------
#ifndef PIN_LINK_SCLK
#define PIN_LINK_SCLK 11
#endif
#ifndef PIN_LINK_MOSI
#define PIN_LINK_MOSI 12
#endif
#ifndef PIN_LINK_IRQ
#define PIN_LINK_IRQ  13
#endif
#ifndef PIN_LINK_CS
#define PIN_LINK_CS   14
#endif

// SPI2_HOST is free on the S3 as long as nothing else claims it. CTR has no
// other SPI peripheral: no SD card, no display, no second radio.
#define LINK_SPI_HOST SPI2_HOST

static spi_device_handle_t gLinkSpi = nullptr;
static volatile bool       gLinkUp   = false;
static uint32_t            gLinkFramesSent     = 0;
static uint32_t            gLinkFramesDropped  = 0;
static uint32_t            gLinkCsTimeouts     = 0;
static uint32_t            gLinkLastFrameUs    = 0;

// ---- Init -------------------------------------------------------------------
// Called once from setup() on core 0, BEFORE the audioEncode task is created,
// so the bus is never touched from two cores.
inline bool setupLinkSpi() {
  gpio_config_t irqCfg = {};
  irqCfg.pin_bit_mask = (1ULL << PIN_LINK_IRQ);
  irqCfg.mode         = GPIO_MODE_OUTPUT;
  irqCfg.pull_up_en   = GPIO_PULLUP_DISABLE;
  irqCfg.pull_down_en = GPIO_PULLDOWN_ENABLE;  // idle low: no frame ready
  irqCfg.intr_type    = GPIO_INTR_DISABLE;
  if (gpio_config(&irqCfg) != ESP_OK) return false;
  gpio_set_level((gpio_num_t)PIN_LINK_IRQ, 0);

  gpio_config_t csCfg = {};
  csCfg.pin_bit_mask = (1ULL << PIN_LINK_CS);
  csCfg.mode         = GPIO_MODE_INPUT;
  csCfg.pull_up_en   = GPIO_PULLUP_ENABLE;     // STR drives it low; idle high
  csCfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  csCfg.intr_type    = GPIO_INTR_DISABLE;
  if (gpio_config(&csCfg) != ESP_OK) return false;

  spi_bus_config_t bus = {};
  bus.mosi_io_num     = PIN_LINK_MOSI;
  bus.miso_io_num     = -1;                    // one-way: no return data
  bus.sclk_io_num     = PIN_LINK_SCLK;
  bus.quadwp_io_num   = -1;
  bus.quadhd_io_num   = -1;
  bus.max_transfer_sz = LINK_HDR_BYTES + LINK_MAX_PAYLOAD;
  if (spi_bus_initialize(LINK_SPI_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
    return false;
  }

  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = LINK_SPI_HZ;
  dev.mode           = 0;                      // CPOL=0, CPHA=0
  dev.spics_io_num   = -1;                     // CS is a GPIO handshake
  dev.queue_size     = 1;
  dev.flags          = SPI_DEVICE_NO_DUMMY;
  if (spi_bus_add_device(LINK_SPI_HOST, &dev, &gLinkSpi) != ESP_OK) {
    return false;
  }

  gLinkUp = true;
  return true;
}

// The CS line is driven by EVDSTR and pulled up here, so a partner that is
// present reads HIGH at idle and a missing one also reads HIGH - the useful
// test is the traffic, not the level. This prints what the boot state actually
// is, so "link IDLE" in the heartbeat is not the first sign of a problem.
inline void logLinkPeerState(const char *when) {
  int cs  = gpio_get_level((gpio_num_t)PIN_LINK_CS);
  int irq = gpio_get_level((gpio_num_t)PIN_LINK_IRQ);
  Serial.printf("[LINK] %s: CS(GPIO%d)=%d idle, IRQ(GPIO%d)=%d; "
                "frames out %lu, dropped %lu\n",
                when, PIN_LINK_CS, cs, PIN_LINK_IRQ, irq,
                (unsigned long)statLinkSent, (unsigned long)statLinkDropped);
}

// ---- Frame send -------------------------------------------------------------
// Blocks at most LINK_CS_TIMEOUT_US + LINK_XFER_TIMEOUT_US. Returns true when
// STR clocked the whole frame. On false the caller must count a drop and move
// on: never retry, never wait — the next frame is 20 ms away.
inline bool linkSendFrame(const uint8_t *payload, uint32_t len,
                          uint8_t samples, uint8_t channels,
                          uint32_t rateHz, bool silence) {
  if (!gLinkUp || !gLinkSpi) return false;
  if (len > LINK_MAX_PAYLOAD) { gLinkFramesDropped++; return false; }

  LinkFrameHeader h;
  linkFillHeader(h, samples, channels, rateHz, silence);
  const uint32_t total = LINK_HDR_BYTES + len;

  // One transaction, header then payload back to back, so STR clocks exactly
  // `total` bytes after raising CS. Two transactions would leave a gap in the
  // clock that STR cannot tell from the end of the frame.
  static uint8_t txBuf[LINK_HDR_BYTES + LINK_MAX_PAYLOAD];
  memcpy(txBuf, &h, LINK_HDR_BYTES);
  if (len) memcpy(txBuf + LINK_HDR_BYTES, payload, len);

  // 1. Announce. The setup delay matters: STR runs a GPIO ISR or a tight
  //    poll, and the edge must be visible before the first SCLK byte leaves.
  gpio_set_level((gpio_num_t)PIN_LINK_IRQ, 1);
  delayMicroseconds(LINK_IRQ_SETUP_US);

  // 2. Wait for STR to pull CS low.
  const uint32_t tCs = micros();
  while (gpio_get_level((gpio_num_t)PIN_LINK_CS) != 0) {
    if ((uint32_t)(micros() - tCs) > LINK_CS_TIMEOUT_US) {
      gpio_set_level((gpio_num_t)PIN_LINK_IRQ, 0);
      gLinkCsTimeouts++;
      gLinkFramesDropped++;
      return false;
    }
  }

  // 3. Clock it out. spi_device_polling_transmit() returns when the last bit
  //    has left the shifter, which is the earliest point STR can raise CS.
  spi_transaction_t t = {};
  t.length    = total * 8;          // in bits
  t.tx_buffer = txBuf;
  t.rx_buffer = nullptr;
  const uint32_t tX = micros();
  esp_err_t rc = spi_device_polling_transmit(gLinkSpi, &t);
  gLinkLastFrameUs = (uint32_t)(micros() - tX);

  // 4. Release. IRQ goes low whether or not the transfer succeeded; a stuck
  //    high IRQ would make STR believe a frame is still coming.
  gpio_set_level((gpio_num_t)PIN_LINK_IRQ, 0);

  if (rc != ESP_OK) { gLinkFramesDropped++; return false; }

  // 5. The real confirmation: STR raises CS only after it has taken every
  //    byte. If CS is still low, STR aborted mid-frame (CRC of its own, heap
  //    failure, reset) and this frame is gone.
  const uint32_t tDone = micros();
  while (gpio_get_level((gpio_num_t)PIN_LINK_CS) == 0) {
    if ((uint32_t)(micros() - tDone) > LINK_XFER_TIMEOUT_US) {
      gLinkFramesDropped++;
      return false;
    }
  }

  gLinkFramesSent++;
  return true;
}
