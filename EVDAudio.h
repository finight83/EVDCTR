#pragma once
// ============================================================================
//  EVDAudio.h - EVDCTR audio pipeline: capture -> pre-process -> encode -> link
// ============================================================================
//
//  Three tasks, two cores, one rule: opus_encode() runs alone on core 1.
//
//    CORE 0
//      audioReadTask        I2S DMA drain / USB read / network yield
//                           -> rawQueue
//      audioProcessingTask  rate estimate, two-stage resample, anti-alias,
//                           quiet filters, volume, frame assembly
//                           -> encodeQueue
//
//    CORE 1
//      audioEncodeTask      encodeQueue -> opus_encode() -> linkSendFrame()
//
//  audioEncodeTask's loop body is: roll over the 1 s counters, drain a pending
//  encoder re-create, receive one frame, apply pending ctl settings, encode,
//  hand the payload to the link. Nothing else. No JSON, no String, no WiFi, no
//  NVS, no logging at frame rate.
//
//  On a link failure (STR not booted, wrong wiring, no ground) the frame is
//  counted and dropped. The encode core is never blocked longer than
//  LINK_CS_TIMEOUT_US + LINK_XFER_TIMEOUT_US (10 ms), which is half a frame.
// ============================================================================

// ---- Stage-1 / stage-2 resampler ------------------------------------------
// Catmull-Rom cubic with a ratio-1 fast path. Unchanged from the unsplit
// server: it is the same source hardware feeding the same encoder.
class ResamplerToOpusRate {
 public:
  void reset() { pos_ = 0.0; primed_ = false; }
  void setRatio(double r) {
    if (r <= 0.0) r = 1.0;
    if (r != ratio_) { ratio_ = r; reset(); }
  }
  double ratio() const { return ratio_; }

  uint32_t process(const int16_t *in, uint32_t inFrames,
                   int16_t *out, uint32_t outCapFrames) {
    if (ratio_ == 1.0) {
      uint32_t n = inFrames < outCapFrames ? inFrames : outCapFrames;
      memcpy(out, in, n * CHANNELS * sizeof(int16_t));
      return n;
    }
    // Down-sampling needs a 4-tap window (i-1 .. i+2), so a single-frame chunk
    // would index in[i+1] out of bounds. Nothing to do with no output possible.
    if (inFrames < 2) return 0;
    if (!primed_) {
      last_[0] = in[0]; last_[1] = in[1];
      prev_[0] = in[0]; prev_[1] = in[1];
      primed_ = true;
    }
    uint32_t produced = 0;
    while (pos_ < (double)(inFrames - 1) && produced < outCapFrames) {
      uint32_t i = (uint32_t)pos_;
      double   t = pos_ - (double)i;
      for (int ch = 0; ch < CHANNELS; ++ch) {
        double p0 = (i >= 1) ? (double)in[(i - 1) * CHANNELS + ch]
                             : (double)prev_[ch];
        double p1 = (double)in[i * CHANNELS + ch];
        double p2 = (double)in[(i + 1) * CHANNELS + ch];
        double p3 = (i + 2 < inFrames) ? (double)in[(i + 2) * CHANNELS + ch]
                                       : (double)last_[ch];
        double v = 0.5 * ((2.0 * p1) +
                          (-p0 + p2) * t +
                          (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t * t +
                          (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t * t * t);
        if (v >  32767.0) v =  32767.0;
        if (v < -32768.0) v = -32768.0;
        out[produced * CHANNELS + ch] = (int16_t)v;
      }
      produced++;
      pos_ += ratio_;
    }
    pos_ -= (double)(inFrames - 1);
    if (pos_ < 0.0) pos_ = 0.0;
    last_[0] = in[(inFrames - 1) * CHANNELS + 0];
    last_[1] = in[(inFrames - 1) * CHANNELS + 1];
    prev_[0] = in[0];
    prev_[1] = in[1];
    return produced;
  }

 private:
  double ratio_ = 1.0;
  double pos_   = 0.0;
  bool   primed_ = false;
  int16_t last_[CHANNELS] = {0, 0};
  int16_t prev_[CHANNELS] = {0, 0};
};

ResamplerToOpusRate resampler;                // source -> 48 kHz content
ResamplerToOpusRate resamplerContentToOpus;   // 48 kHz content -> encoder rate

// ---- Pending frame assembly ------------------------------------------------
static int16_t pendingBuf[PENDING_MAX_FRAMES * CHANNELS];
static uint32_t pendingCount           = 0;
static uint32_t pendingOldestCapturedMs = 0;

// ---- Quiet-mode filters ----------------------------------------------------
struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  double z1[CHANNELS] = {0, 0}, z2[CHANNELS] = {0, 0};
  void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
  inline double step(double x, int ch) {
    double y = b0 * x + z1[ch];
    z1[ch] = b1 * x - a1 * y + z2[ch];
    z2[ch] = b2 * x - a2 * y;
    return y;
  }
};

static Biquad  quietHighPass;
static Biquad  quietLowShelf;
static Biquad  antiAlias24k;
static bool    antiAliasActive = false;
static double  quietPeakEnv = 0.0;

inline void configureQuietFilters() {
  // 80 Hz 2nd-order high-pass: removes rumble and DC, leaves voices intact.
  const double fs = 48000.0, fc = 80.0;
  const double w0 = 2.0 * M_PI * fc / fs, alpha = sin(w0) / (2.0 * 0.707);
  const double cw = cos(w0), a0 = 1.0 + alpha;
  quietHighPass.b0 = (1.0 + cw) / 2.0 / a0;
  quietHighPass.b1 = -(1.0 + cw) / a0;
  quietHighPass.b2 = (1.0 + cw) / 2.0 / a0;
  quietHighPass.a1 = -2.0 * cw / a0;
  quietHighPass.a2 = (1.0 - alpha) / a0;
  quietHighPass.reset();

  // -3 dB low shelf at 3 kHz: tames sibilance at low listening levels.
  const double f2 = 3000.0, gainDb = -3.0, S = 1.0;
  const double A = pow(10.0, gainDb / 40.0);
  const double w = 2.0 * M_PI * f2 / fs;
  const double alphaS = sin(w) / 2.0 * sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
  const double cw2 = cos(w), sq = 2.0 * sqrt(A) * alphaS;
  const double a0b = (A + 1.0) + (A - 1.0) * cw2 + sq;
  quietLowShelf.b0 = A * ((A + 1.0) - (A - 1.0) * cw2 + sq) / a0b;
  quietLowShelf.b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw2) / a0b;
  quietLowShelf.b2 = A * ((A + 1.0) - (A - 1.0) * cw2 - sq) / a0b;
  quietLowShelf.a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw2) / a0b;
  quietLowShelf.a2 = ((A + 1.0) + (A - 1.0) * cw2 - sq) / a0b;
  quietLowShelf.reset();

  // 10 kHz Butterworth low-pass, used only while the encoder runs at 24 kHz.
  const double fc3 = 10000.0;
  const double w3 = 2.0 * M_PI * fc3 / fs;
  const double alpha3 = sin(w3) / (2.0 * 0.7071), cw3 = cos(w3);
  const double a03 = 1.0 + alpha3;
  antiAlias24k.b0 = (1.0 - cw3) / 2.0 / a03;
  antiAlias24k.b1 = (1.0 - cw3) / a03;
  antiAlias24k.b2 = (1.0 - cw3) / 2.0 / a03;
  antiAlias24k.a1 = -2.0 * cw3 / a03;
  antiAlias24k.a2 = (1.0 - alpha3) / a03;
  antiAlias24k.reset();

  LOGI("quiet filters configured");
}

inline void applyQuietFilter(int16_t *buf, uint32_t frames) {
  if (!quietModeEnabled) return;
  for (uint32_t i = 0; i < frames; ++i) {
    for (int ch = 0; ch < CHANNELS; ++ch) {
      double x = (double)buf[i * CHANNELS + ch];
      double y = quietLowShelf.step(quietHighPass.step(x, ch), ch);
      if (y >  32767.0) y =  32767.0;
      if (y < -32768.0) y = -32768.0;
      buf[i * CHANNELS + ch] = (int16_t)y;
    }
  }
}

// Peak limiter: only engages above -1 dBFS, so normal material is untouched.
inline void applyQuietDynamics(int16_t *buf, uint32_t frames) {
  if (!quietModeEnabled) return;
  for (uint32_t i = 0; i < frames * CHANNELS; ++i) {
    double a = fabs((double)buf[i]);
    if (a > quietPeakEnv) quietPeakEnv = a;
    else quietPeakEnv *= 0.9995;
    if (quietPeakEnv > 30000.0) {
      double g = 30000.0 / quietPeakEnv;
      buf[i] = (int16_t)((double)buf[i] * g);
    }
  }
}

inline void applyAntiAlias24k(int16_t *buf, uint32_t frames) {
  for (uint32_t i = 0; i < frames; ++i) {
    for (int ch = 0; ch < CHANNELS; ++ch) {
      double y = antiAlias24k.step((double)buf[i * CHANNELS + ch], ch);
      if (y >  32767.0) y =  32767.0;
      if (y < -32768.0) y = -32768.0;
      buf[i * CHANNELS + ch] = (int16_t)y;
    }
  }
}

inline void serviceAntiAliasFilter() {
  bool want = (g_opusSampleRate == OPUS_SAMPLE_RATE_24K);
  if (want != antiAliasActive) {
    antiAlias24k.reset();
    antiAliasActive = want;
  }
}

// ---- Input rate estimator --------------------------------------------------
// The source board's I2S clock is not exactly 48 kHz, and BT codec
// renegotiation can change it to 44.1 kHz mid-stream. We track the measured
// rate against the sample-count timeline and only act on a persistent
// mismatch; a single late chunk is jitter, not a rate change.
static uint32_t rateWinFrames = 0;
static uint32_t rateWinStartMs = 0;
static double   rateEstimate  = (double)SOURCE_SAMPLE_RATE;

inline void updateInputRateEstimate(uint32_t inFrames, uint32_t capturedMs) {
  if (rateWinStartMs == 0) { rateWinStartMs = capturedMs; rateWinFrames = 0; }
  rateWinFrames += inFrames;
  uint32_t elapsed = capturedMs - rateWinStartMs;
  if (elapsed < 3000) return;

  double measured = (double)rateWinFrames * 1000.0 / (double)elapsed;
  rateWinStartMs = capturedMs;
  rateWinFrames  = 0;
  if (measured < 8000.0 || measured > 192000.0) return;

  double dev = (measured - rateEstimate) / rateEstimate;
  if (fabs(dev) > 0.03) {
    LOGW("source rate change: %.0f -> %.0f Hz", rateEstimate, measured);
    rateEstimate = measured;
    g_sourceSampleRate = (uint32_t)(measured + 0.5);
    g_rateEstimatorNeedsReset = true;
    // The measured rate only matters to the stage-1 resampler; the encoder
    // rate is a separate, operator-controlled setting.
    resampler.setRatio(rateEstimate / 48000.0);
  } else {
    statDriftPpm = (uint32_t)(fabs(dev) * 1e6);
  }
}

// ---- Opus setup ------------------------------------------------------------
OpusEncoder *opusEncoder = nullptr;

inline void setup_opus(uint32_t sampleRate) {
  if (sampleRate != OPUS_SAMPLE_RATE_48K && sampleRate != OPUS_SAMPLE_RATE_24K) {
    LOGW("setup_opus: %u out of range, using 48 kHz", (unsigned)sampleRate);
    sampleRate = OPUS_SAMPLE_RATE_48K;
  }
  if (opusEncoder) { opus_encoder_destroy(opusEncoder); opusEncoder = nullptr; }

  int err = OPUS_OK;
  opusEncoder = opus_encoder_create((opus_int32)sampleRate, CHANNELS,
                                    g_newApplication, &err);
  if (err != OPUS_OK || !opusEncoder) {
    LOGE("opus_encoder_create failed: %s", opus_strerror(err));
    opusEncoder = nullptr;
    return;
  }
  opus_encoder_ctl(opusEncoder, OPUS_SET_BITRATE(g_newBitrate));
  opus_encoder_ctl(opusEncoder, OPUS_SET_COMPLEXITY(
      g_pendingComplexity >= 0 ? (int)g_pendingComplexity : currentOpusComplexity));
  opus_encoder_ctl(opusEncoder, OPUS_SET_VBR(g_newVbr));
  opus_encoder_ctl(opusEncoder, OPUS_SET_VBR_CONSTRAINT(g_newVbrConstraint));
  opus_encoder_ctl(opusEncoder, OPUS_SET_SIGNAL(g_newSignal));
  opus_encoder_ctl(opusEncoder, OPUS_SET_INBAND_FEC(1));
  opus_encoder_ctl(opusEncoder, OPUS_SET_PACKET_LOSS_PERC(5));

  g_opusSampleRate    = sampleRate;
  g_opusFrameSamples  = (uint16_t)(sampleRate / (1000 / OPUS_FRAME_MS));
  g_currentBitrate    = g_newBitrate;
  g_currentVbr        = g_newVbr;
  g_currentVbrConstraint = g_newVbrConstraint;
  g_currentSignal     = g_newSignal;
  g_currentApplication= g_newApplication;
  currentOpusComplexity = (g_pendingComplexity >= 0) ? (int)g_pendingComplexity
                                                     : currentOpusComplexity;
  g_pendingOpusSampleRate = 0;
  g_rateChangeFlushPending = true;
  LOGI("opus encoder: %u Hz, %u samples/frame, %d bps, cx%d",
       (unsigned)sampleRate, (unsigned)g_opusFrameSamples, g_currentBitrate,
       currentOpusComplexity);
}

// Encoder-only re-create. Runs on core 1 inside audioEncodeTask, which is the
// only place libopus is touched, so no lock is needed.
inline void handleEncoderRecreate() {
  uint32_t newRate = g_pendingOpusSampleRate;
  g_pendingOpusSampleRate = 0;
  if (newRate == 0xFFFFFFFFu) newRate = g_opusSampleRate;   // app change only
  if (newRate == 0) return;

  uint32_t oldRate = g_opusSampleRate;
  setup_opus(newRate);
  if (!opusEncoder) setup_opus(oldRate);   // fail closed: back to what worked

  // A rate change invalidates any frame still in the queue: its sample count
  // no longer matches the encoder.
  if (encodeQueue) {
    EncodeFrame drain;
    while (xQueueReceive(encodeQueue, &drain, 0) == pdTRUE) { }
  }
}

// ---- I2S input -------------------------------------------------------------
inline bool setup_i2s_in() {
  i2s_config_t cfg = {};
  cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate          = SOURCE_SAMPLE_RATE;
  cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count        = 8;
  cfg.dma_buf_len          = 256;
  cfg.use_apll             = false;
  cfg.tx_desc_auto_clear   = false;
  cfg.fixed_mclk           = 0;

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = pinBtBclk;
  pins.ws_io_num    = pinBtLrc;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num  = pinBtDin;

  esp_err_t e1 = i2s_driver_install(I2S_PORT_IN, &cfg, 0, nullptr);
  if (e1 != ESP_OK) { LOGE("i2s_driver_install failed: %d", (int)e1); return false; }
  esp_err_t e2 = i2s_set_pin(I2S_PORT_IN, &pins);
  if (e2 != ESP_OK) { LOGE("i2s_set_pin failed: %d", (int)e2); return false; }
  LOGI("I2S input ready (bclk %d, lrc %d, din %d)", pinBtBclk, pinBtLrc, pinBtDin);
  return true;
}

// ---- audioReadTask (core 0) ------------------------------------------------
inline void audioReadTask(void *) {
  LOGI("audioReadTask started on core %d", xPortGetCoreID());
  static uint8_t raw[I2S_READ_CHUNK_BYTES];
  RawChunk chunk;

  for (;;) {
    if (otaPauseAudio) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

    if (serverInputSource == SRC_FROM_USB) {
      // USB audio arrives through EVDUsb.h, which pushes into rawQueue itself.
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if (serverInputSource == SRC_FROM_NETWORK) {
      // DLNA fetch task (core 0) is the producer.
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    size_t bytesRead = 0;
    esp_err_t rc = i2s_read(I2S_PORT_IN, raw, sizeof(raw), &bytesRead,
                            pdMS_TO_TICKS(100));
    if (rc != ESP_OK || bytesRead == 0) continue;

    chunk.len         = (uint32_t)bytesRead;
    chunk.captured_ms = millis();
    memcpy(chunk.data, raw, bytesRead);
    if (xQueueSend(rawQueue, &chunk, pdMS_TO_TICKS(20)) != pdTRUE) {
      statRawDropped++;
      gRateFramesLostTotal += bytesRead / (2 * sizeof(int16_t));
    }
  }
}

// ---- audioProcessingTask (core 0) -----------------------------------------
// Produces one fully pre-processed PCM frame every 20 ms. It never touches
// the encoder and never touches the link.
inline void audioProcessingTask(void *) {
  LOGI("audioProcessingTask started on core %d", xPortGetCoreID());
  static int16_t contentScratch[2048];
  static int16_t resampledScratch[2048];
  RawChunk chunk;

  resampler.reset();
  resamplerContentToOpus.reset();
  resamplerContentToOpus.setRatio(48000.0 / (double)g_opusSampleRate);
  statDriftPpm = 0;

  bool wasPaused = false;
  for (;;) {
    if (otaPauseAudio) {
      if (!wasPaused) { wasPaused = true; g_rateEstimatorNeedsReset = true; }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    wasPaused = false;

    // Rate-change side-effects set by handleEncoderRecreate() on core 1.
    if (g_rateChangeFlushPending) {
      g_rateChangeFlushPending = false;
      pendingCount = 0;
      pendingOldestCapturedMs = 0;
      resamplerContentToOpus.reset();
      resamplerContentToOpus.setRatio(48000.0 / (double)g_opusSampleRate);
    }
    if (g_rateEstimatorNeedsReset) {
      g_rateEstimatorNeedsReset = false;
      rateWinStartMs = 0;
      rateWinFrames  = 0;
      rateEstimate  = (double)g_sourceSampleRate;
      resampler.setRatio(rateEstimate / 48000.0);
    }
    if (g_quietFiltersNeedReconfig) {
      g_quietFiltersNeedReconfig = false;
      configureQuietFilters();
    }

    if (xQueueReceive(rawQueue, &chunk, portMAX_DELAY) != pdTRUE) continue;

    uint32_t inFrames = chunk.len / (2 * sizeof(int16_t));
    const int16_t *inSamples = (const int16_t *)chunk.data;

    updateInputRateEstimate(inFrames, chunk.captured_ms);

    const uint32_t chunkDurMs =
        (uint32_t)(((uint64_t)inFrames * 1000ULL) / g_sourceSampleRate);
    uint32_t chunkStartMs = (chunk.captured_ms > chunkDurMs)
                            ? (chunk.captured_ms - chunkDurMs) : 0;
    if (pendingCount == 0) {
      // Re-anchor only on boot or a genuine >100 ms interruption. The
      // sample-count timeline is authoritative; per-chunk arrival jitter
      // must not rewrite it (this is the micro-stutter fix from the
      // unsplit server, kept verbatim).
      if (pendingOldestCapturedMs == 0) {
        pendingOldestCapturedMs = chunkStartMs;
      } else {
        int32_t drift = (int32_t)(chunkStartMs - pendingOldestCapturedMs);
        if (drift > 100 || drift < -100) pendingOldestCapturedMs = chunkStartMs;
      }
    }

    uint32_t contentFrames = resampler.process(
        inSamples, inFrames, contentScratch,
        sizeof(contentScratch) / (2 * sizeof(int16_t)));

    serviceAntiAliasFilter();
    if (g_opusSampleRate == OPUS_SAMPLE_RATE_24K) {
      applyAntiAlias24k(contentScratch, contentFrames);
    }

    uint32_t produced = resamplerContentToOpus.process(
        contentScratch, contentFrames, resampledScratch,
        sizeof(resampledScratch) / (2 * sizeof(int16_t)));

    if (produced > (PENDING_MAX_FRAMES - pendingCount)) {
      uint32_t drop = produced - (PENDING_MAX_FRAMES - pendingCount);
      if (drop >= pendingCount) {
        drop = pendingCount;
        pendingCount = 0;
        pendingOldestCapturedMs = chunkStartMs;
      } else {
        uint32_t keep = pendingCount - drop;
        memmove(pendingBuf, pendingBuf + drop * 2, keep * 2 * sizeof(int16_t));
        pendingCount = keep;
        pendingOldestCapturedMs +=
            (uint32_t)((uint64_t)drop * 1000ULL / g_opusSampleRate);
      }
      portENTER_CRITICAL(&gStatsMux);
        statPendingBufDroppedTotal   += drop;
        statPendingBufDroppedThisSec += drop;
      portEXIT_CRITICAL(&gStatsMux);
    }
    for (uint32_t i = 0; i < produced && pendingCount < PENDING_MAX_FRAMES; i++) {
      pendingBuf[pendingCount * 2 + 0] = resampledScratch[i * 2 + 0];
      pendingBuf[pendingCount * 2 + 1] = resampledScratch[i * 2 + 1];
      pendingCount++;
    }
    portENTER_CRITICAL(&gStatsMux);
      if (pendingCount > statPendingBufPeakDepth)
        statPendingBufPeakDepth = pendingCount;
    portEXIT_CRITICAL(&gStatsMux);

    // Emit ready frames: all pre-processing happens HERE, on core 0.
    while (pendingCount >= g_opusFrameSamples) {
      if (quietModeEnabled) {
        applyQuietFilter(pendingBuf, g_opusFrameSamples);
        applyQuietDynamics(pendingBuf, g_opusFrameSamples);
      }

      uint8_t effectiveVolume =
          (uint8_t)(((uint16_t)masterVolume * potVolume) / 100);
      if (effectiveVolume < 100) {
        for (uint32_t i = 0; i < g_opusFrameSamples * CHANNELS; ++i)
          pendingBuf[i] = (int16_t)((int32_t)pendingBuf[i] * effectiveVolume / 100);
      }

      static EncodeFrame ef;   // BSS, not stack: 3.8 KB
      ef.captured_ms = pendingOldestCapturedMs;
      ef.seq         = gAudioFrameSeq++;
      ef.samples     = g_opusFrameSamples;
      memcpy(ef.pcm, pendingBuf, g_opusFrameSamples * CHANNELS * sizeof(int16_t));

      if (xQueueSend(encodeQueue, &ef, 0) != pdTRUE) statOpusDropped++;

      uint32_t remaining = pendingCount - g_opusFrameSamples;
      memmove(pendingBuf, pendingBuf + g_opusFrameSamples * 2,
              remaining * 2 * sizeof(int16_t));
      pendingCount = remaining;
      pendingOldestCapturedMs += OPUS_FRAME_MS;
    }
  }
}

// ---- audioEncodeTask (core 1) - the only occupant of core 1 ----------------
inline void audioEncodeTask(void *) {
  LOGI("audioEncodeTask started on core %d (sole core-1 occupant)",
       xPortGetCoreID());
  static EncodeFrame ef;
  static OpusPacket  pkt;

  for (;;) {
    if (otaPauseAudio) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

    // 1 s counter rollover. Cheap, and the only place these are written.
    {
      static uint32_t lastRoll = 0;
      uint32_t now = millis();
      if (now - lastRoll >= 1000) {
        lastRoll = now;
        portENTER_CRITICAL(&gStatsMux);
          statPendingBufDroppedLastSec   = statPendingBufDroppedThisSec;
          statPendingBufDroppedThisSec   = 0;
          statPendingBufPeakDepthLastSec = statPendingBufPeakDepth;
          statPendingBufPeakDepth        = 0;
        portEXIT_CRITICAL(&gStatsMux);
        statEncodeOverrunsLastSec = statEncodeOverrunsThisSec;
        statEncodeOverrunsThisSec = 0;
        statEncodePeakUsLastSec   = statEncodePeakUsThisSec;
        statEncodePeakUsThisSec   = 0;
      }
    }

    // Drain a pending rate re-create before taking the next frame.
    if (g_pendingOpusSampleRate != 0) handleEncoderRecreate();

    if (xQueueReceive(encodeQueue, &ef, portMAX_DELAY) != pdTRUE) continue;

    if (opusEncoder == nullptr) {
      static unsigned long lastWarn = 0;
      unsigned long now = millis();
      if (now - lastWarn > 1000) {
        lastWarn = now;
        LOGE("audioEncodeTask: no encoder; dropping frames");
      }
      continue;
    }
    // A frame produced under the OLD frame size (the other sample rate) must be
    // discarded, and on its own it would be: the encode queue is drained by
    // handleEncoderRecreate(). But a producer that keeps emitting the old size
    // (a stuck g_opusFrameSamples, a re-create that never took) would make this
    // `continue` swallow EVERY frame forever, silently, with the link "up" and
    // the players hearing nothing. Say so instead of dropping quietly.
    if (ef.samples != g_opusFrameSamples) {
      static unsigned long lastMismatch = 0;
      unsigned long now = millis();
      if (now - lastMismatch > 2000) {
        lastMismatch = now;
        LOGE("encode: frame of %u samples, encoder expects %u - dropped "
             "(stale producer?)", (unsigned)ef.samples,
             (unsigned)g_opusFrameSamples);
      }
      statOpusDropped++;
      continue;
    }

    if (g_pendingComplexity >= 0) {
      int c = g_pendingComplexity;
      g_pendingComplexity = -1;
      if (c != currentOpusComplexity) {
        opus_encoder_ctl(opusEncoder, OPUS_SET_COMPLEXITY(c));
        currentOpusComplexity = c;
      }
    }

    if (g_encoderSettingsDirty) {
      g_encoderSettingsDirty = false;
      // Application mode cannot be changed with a ctl call: re-create.
      if (g_newApplication != g_currentApplication) {
        g_currentApplication = g_newApplication;
        g_pendingOpusSampleRate = 0xFFFFFFFFu;
      }
      if (g_newBitrate != g_currentBitrate) {
        opus_encoder_ctl(opusEncoder, OPUS_SET_BITRATE(g_newBitrate));
        g_currentBitrate = g_newBitrate;
      }
      if (g_newVbr != g_currentVbr) {
        opus_encoder_ctl(opusEncoder, OPUS_SET_VBR(g_newVbr));
        g_currentVbr = g_newVbr;
      }
      if (g_newVbrConstraint != g_currentVbrConstraint) {
        opus_encoder_ctl(opusEncoder, OPUS_SET_VBR_CONSTRAINT(g_newVbrConstraint));
        g_currentVbrConstraint = g_newVbrConstraint;
      }
      if (g_newSignal != g_currentSignal) {
        opus_encoder_ctl(opusEncoder, OPUS_SET_SIGNAL(g_newSignal));
        g_currentSignal = g_newSignal;
      }
    }

    // 2. Encode.
    uint32_t t0 = micros();
    int nbytes = opus_encode(opusEncoder, ef.pcm, ef.samples,
                             pkt.data, OPUS_MAX_PACKET_BYTES);
    uint32_t encodeUs = micros() - t0;
    gAvgEncodeUs = (gAvgEncodeUs * 63 + encodeUs) >> 6;

    if (encodeUs > 20000) {
      statEncodeOverrunsTotal++;
      statEncodeOverrunsThisSec++;
      static uint32_t lastThrottle = 0;
      if (millis() - lastThrottle > 10000 && g_pendingComplexity > 0) {
        lastThrottle = millis();
        g_pendingComplexity--;
        LOGW("encode overrun: complexity -> %d", g_pendingComplexity);
      }
    }
    if (encodeUs > statEncodePeakUsThisSec) statEncodePeakUsThisSec = encodeUs;

    if (nbytes < 0) {
      static unsigned long lastErr = 0;
      unsigned long now = millis();
      if (now - lastErr > 1000) {
        lastErr = now;
        LOGE("opus_encode failed: %d", nbytes);
      }
      statOpusDropped++;
      continue;
    }

    // 3. Hand it to the link. This is the ONLY inter-chip step, and it is a
    //    bounded-time SPI transfer: <= 10 ms worst case, 1 ms typical.
    pkt.len     = nbytes;
    pkt.samples = ef.samples;
    bool ok = linkSendFrame(pkt.data, (uint32_t)nbytes, (uint8_t)ef.samples,
                            CHANNELS, g_opusSampleRate, false);
    if (ok) {
      statOpusEncoded++;
      statLinkSent = gLinkFramesSent;
      gLinkLastOkMs = millis();
      gLinkHealthy  = true;
      // Published for the Stream tab: how long the SPI transfer itself took.
      statLinkLastFrameUs = gLinkLastFrameUs;
    } else {
      statOpusDropped++;
      statLinkDropped = gLinkFramesDropped;
      statLinkCsTimeouts = gLinkCsTimeouts;
      gLinkHealthy = false;
    }
  }
}

// ---- setup -----------------------------------------------------------------
// Prints the encoder's own counters once every 5 s while nothing else is
// consuming them. Cheap: four volatile reads and one line.
inline void encodeProbeTask(void *) {
  uint32_t lastEnc = 0, lastSent = 0, lastDrop = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    uint32_t enc  = statOpusEncoded;
    uint32_t sent = statLinkSent;
    uint32_t drop = statLinkDropped;
    Serial.printf("[ENC] %lu frames in 5 s (%.1f fps) | link out +%lu "
                  "dropped +%lu | %s\n",
                  (unsigned long)(enc - lastEnc),
                  (double)(enc - lastEnc) / 5.0,
                  (unsigned long)(sent - lastSent),
                  (unsigned long)(drop - lastDrop),
                  gLinkHealthy ? "EVDSTR clocking frames"
                               : "no EVDSTR clocking - frames are being dropped");
    lastEnc = enc; lastSent = sent; lastDrop = drop;
  }
}

inline void setup_queues_and_tasks() {
  LOGI("Creating queues and tasks...");
  rawQueue    = xQueueCreate(RAW_QUEUE_LEN,    sizeof(RawChunk));
  encodeQueue = xQueueCreate(ENCODE_QUEUE_LEN, sizeof(EncodeFrame));
  opusQueue   = xQueueCreate(OPUS_QUEUE_LEN,   sizeof(OpusPacket));
  if (!rawQueue || !encodeQueue || !opusQueue) {
    // Name which one: an out-of-DRAM boot is the usual cause, and the fix
    // differs (RAW_QUEUE_LEN vs ENCODE_QUEUE_LEN).
    Serial.printf("[TASK] queue creation FAILED: raw=%s enc=%s opus=%s "
                  "(free heap %u B) - no audio will flow\n",
                  rawQueue ? "ok" : "NULL", encodeQueue ? "ok" : "NULL",
                  opusQueue ? "ok" : "NULL", (unsigned)ESP.getFreeHeap());
    LOGE("queue creation failed");
    return;
  }

  BaseType_t rc;
  // Core 0: capture and pre-processing.
  rc = xTaskCreatePinnedToCore(audioReadTask, "audioRead", 4096, nullptr, 4,
                               nullptr, 0);
  if (rc != pdPASS) { LOGE("FAILED to create audioReadTask (%d)", (int)rc);
                      Serial.printf("[TASK] audioRead  FAILED (%d)\n", (int)rc); }

  rc = xTaskCreatePinnedToCore(audioProcessingTask, "audioProc", 8192, nullptr,
                               2, nullptr, 0);
  if (rc != pdPASS) { LOGE("FAILED to create audioProcessingTask (%d)", (int)rc);
                      Serial.printf("[TASK] audioProc  FAILED (%d)\n", (int)rc); }

  // Core 1: encode + link send. Sole occupant, by design.
  rc = xTaskCreatePinnedToCore(audioEncodeTask, "audioEncode", 8192, nullptr,
                               5, nullptr, 1);
  if (rc != pdPASS) { LOGE("FAILED to create audioEncodeTask (%d)", (int)rc);
                      Serial.printf("[TASK] audioEncode FAILED (%d)\n", (int)rc); }

  // ---- Standalone proof ----------------------------------------------------
  // With no EVDSTR on the other end of the pin link, the encoder still runs -
  // and its output is dropped on the wire. That is correct, but it makes a
  // board with no partner look identical to a board with nothing to encode.
  // Count what the encoder actually produced and report it every 5 s, so a
  // lone EVDCTR proves its whole capture -> resample -> opus chain in a serial
  // monitor without a second board.
  rc = xTaskCreatePinnedToCore(encodeProbeTask, "encProbe", 3072, nullptr,
                               1, nullptr, 0);
  if (rc != pdPASS) LOGE("FAILED to create encodeProbeTask (%d)", (int)rc);

  Serial.printf("[TASK] core 0: audioRead(p4) audioProc(p2) http dlna | "
                "core 1: audioEncode(p5)\n");
  LOGI("Task layout: core 0 = read/proc/HTTP/DLNA, core 1 = encode only");
}
