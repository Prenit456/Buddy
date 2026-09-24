/*
  Separate PlatformIO MAX98357A speaker test.
  Environment: buddy-s3-speaker-test

  No Wi-Fi or API is used. The test continuously plays alternating 660 Hz and
  880 Hz tones through BCLK 18, LRC 21 and DIN 2.
*/

#include <Arduino.h>
#include <PcmAudio.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <driver/i2s.h>
#include <cmath>

constexpr i2s_port_t kAudioPort = I2S_NUM_0;
constexpr int kBclkPin = 18;
constexpr int kLrcPin = 21;
constexpr int kDataOutPin = 2;
constexpr uint32_t kSampleRate = 16000;
constexpr uint8_t kVolume = 100;
constexpr int16_t kBeepAmplitude = 5000;
constexpr size_t kBlockSamples = 128;

TFT_eSPI tft;

bool beginSpeaker() {
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = kSampleRate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = kBclkPin;
  pins.ws_io_num = kLrcPin;
  pins.data_out_num = kDataOutPin;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  if (i2s_driver_install(kAudioPort, &config, 0, nullptr) != ESP_OK)
    return false;
  if (i2s_set_pin(kAudioPort, &pins) != ESP_OK) {
    i2s_driver_uninstall(kAudioPort);
    return false;
  }
  i2s_zero_dma_buffer(kAudioPort);
  return true;
}

bool writeFrames(const int32_t* frames, size_t frameCount) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(frames);
  size_t remaining = frameCount * 2 * sizeof(int32_t);
  while (remaining) {
    size_t written = 0;
    if (i2s_write(kAudioPort, data, remaining, &written,
                  pdMS_TO_TICKS(500)) != ESP_OK || !written)
      return false;
    data += written;
    remaining -= written;
  }
  return true;
}

bool playTone(double frequency, uint32_t durationMs) {
  double phase = 0.0;
  int32_t frames[kBlockSamples * 2];
  const uint32_t totalSamples = uint64_t(kSampleRate) * durationMs / 1000;
  constexpr uint32_t kFadeSamples = kSampleRate / 100;  // 10 ms each end.
  uint32_t played = 0;

  while (played < totalSamples) {
    const size_t count = min<size_t>(totalSamples - played, kBlockSamples);
    for (size_t i = 0; i < count; ++i) {
      // Keep the test tone below the amplifier's clipping range at 5 V.
      const uint32_t at = played + i;
      double envelope = 1.0;
      if (at < kFadeSamples) envelope = double(at) / kFadeSamples;
      const uint32_t untilEnd = totalSamples - 1 - at;
      if (untilEnd < kFadeSamples)
        envelope = min(envelope, double(untilEnd) / kFadeSamples);
      const int16_t sample = static_cast<int16_t>(
          kBeepAmplitude * envelope * sin(phase));
      buddy_audio::speakerFrame(sample, kVolume, frames + i * 2);
      phase += 2.0 * PI * frequency / kSampleRate;
      if (phase >= 2.0 * PI) phase -= 2.0 * PI;
    }
    if (!writeFrames(frames, count)) return false;
    played += count;
  }
  return true;
}

bool playSilence(uint32_t durationMs) {
  int32_t frames[kBlockSamples * 2] = {};
  uint32_t remaining = uint64_t(kSampleRate) * durationMs / 1000;
  while (remaining) {
    const size_t count = min<size_t>(remaining, kBlockSamples);
    if (!writeFrames(frames, count)) return false;
    remaining -= count;
  }
  return true;
}

void showStatus(const char* title, const char* detail, uint16_t color) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextWrap(true);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(12, 20);
  tft.println(title);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(12, 72);
  tft.println(detail);
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  showStatus("Speaker test", "Starting I2S audio...", TFT_CYAN);

  if (!beginSpeaker()) {
    showStatus("Audio failed", "Check BCLK 18, LRC 21 and DIN 2", TFT_RED);
    Serial.println("I2S speaker initialization failed");
    return;
  }

  char detail[96];
  snprintf(detail, sizeof(detail),
           "Beep amplitude: %d\nMax digital: 32767\n660 / 880 Hz",
           kBeepAmplitude);
  showStatus("Speaker test", detail, TFT_GREEN);
  Serial.println("Speaker test started: BCLK=18, LRC=21, DIN=2");
}

void loop() {
  if (!playTone(660.0, 450) || !playSilence(120) ||
      !playTone(880.0, 450) || !playSilence(700)) {
    showStatus("Audio stopped", "I2S write failed", TFT_RED);
    Serial.println("I2S write failed");
    delay(2000);
  }
}
