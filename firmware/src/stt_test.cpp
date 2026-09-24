/*
  Separate PlatformIO microphone-to-TFT test.
  Environment: buddy-s3-stt-test

  Records three seconds from the I2S microphone, waits three seconds, plays the
  original recording through the MAX98357A speaker, then displays the Care Hub
  transcription on the TFT.
*/

#include <Arduino.h>
#include <HTTPClient.h>
#include <PcmAudio.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <cmath>

// Current Wi-Fi address of the Mac running the temporary Care Hub.
// Change this if the Mac receives a different address.
constexpr const char* kCareHubUrl = "http://192.168.68.109:4173";

constexpr int kMicSck = 18;
constexpr int kMicWs = 21;
constexpr int kMicData = 1;
constexpr int kSpeakerData = 2;
constexpr unsigned kMicShift = 14;
constexpr int32_t kPlaybackPeak = 5000;
constexpr int32_t kCompressionKnee = kPlaybackPeak / 2;
constexpr float kTargetSpeechRms = 3000.0f;
constexpr uint32_t kSampleRate = 16000;
constexpr uint32_t kRecordSeconds = 3;
constexpr size_t kSampleCount = kSampleRate * kRecordSeconds;
constexpr i2s_port_t kAudioPort = I2S_NUM_0;

TFT_eSPI tft;

void showMessage(const String& title, const String& detail = "",
                 uint16_t color = TFT_CYAN) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextWrap(true);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(12, 16);
  tft.println(title);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(12, 62);
  tft.println(detail);
}

void put32(uint8_t* destination, uint32_t value) {
  for (int i = 0; i < 4; ++i)
    destination[i] = (value >> (8 * i)) & 0xff;
}

void makeWavHeader(uint8_t* wav, size_t samples) {
  memset(wav, 0, 44);
  memcpy(wav, "RIFF", 4);
  put32(wav + 4, samples * 2 + 36);
  memcpy(wav + 8, "WAVEfmt ", 8);
  put32(wav + 16, 16);
  wav[20] = 1;
  wav[22] = 1;
  put32(wav + 24, kSampleRate);
  put32(wav + 28, kSampleRate * 2);
  wav[32] = 2;
  wav[34] = 16;
  memcpy(wav + 36, "data", 4);
  put32(wav + 40, samples * 2);
}

bool startAudio() {
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX |
                                       I2S_MODE_TX);
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
  pins.bck_io_num = kMicSck;
  pins.ws_io_num = kMicWs;
  pins.data_out_num = kSpeakerData;
  pins.data_in_num = kMicData;

  if (i2s_driver_install(kAudioPort, &config, 0, nullptr) != ESP_OK)
    return false;
  if (i2s_set_pin(kAudioPort, &pins) != ESP_OK) {
    i2s_driver_uninstall(kAudioPort);
    return false;
  }
  i2s_zero_dma_buffer(kAudioPort);
  return true;
}

bool playPcm(const uint8_t* pcm, size_t bytes, float gain) {
  if (bytes & 1) return false;
  int32_t slots[256];
  while (bytes) {
    const size_t samples = min<size_t>(bytes / 2, 128);
    for (size_t i = 0; i < samples; ++i) {
      const int16_t original = static_cast<int16_t>(
          buddy_audio::le16(pcm + i * 2));
      const int32_t boosted = static_cast<int32_t>(roundf(original * gain));
      const int32_t magnitude = abs(boosted);
      const int32_t limited = magnitude <= kCompressionKnee
          ? magnitude
          : kPlaybackPeak - (kPlaybackPeak - kCompressionKnee) *
                                kCompressionKnee / magnitude;
      const int16_t scaled = static_cast<int16_t>(
          boosted < 0 ? -limited : limited);
      buddy_audio::speakerFrame(scaled, 100, slots + i * 2);
    }

    const uint8_t* output = reinterpret_cast<const uint8_t*>(slots);
    size_t remaining = samples * 8;
    while (remaining) {
      size_t written = 0;
      if (i2s_write(kAudioPort, output, remaining, &written,
                    pdMS_TO_TICKS(500)) != ESP_OK || !written)
        return false;
      output += written;
      remaining -= written;
    }
    pcm += samples * 2;
    bytes -= samples * 2;
  }
  return true;
}

bool repeatRecording(const uint8_t* wav, uint32_t recordedRms) {
  const float gain = min(12.0f, kTargetSpeechRms /
                                 max<uint32_t>(recordedRms, 1));
  Serial.println("Playback gain: " + String(gain, 2));
  if (!playPcm(wav + 44, kSampleCount * 2, gain)) return false;
  uint8_t silence[512] = {};
  for (int i = 0; i < 9; ++i)
    if (!playPcm(silence, sizeof(silence), gain)) return false;
  return true;
}

bool recordWav(uint8_t* wav, uint32_t& rms, int32_t& peak) {
  auto* pcm = reinterpret_cast<int16_t*>(wav + 44);
  int32_t input[512];
  size_t captured = 0;
  uint64_t energy = 0;
  peak = 0;

  size_t discarded = 0;
  for (int i = 0; i < 8; ++i)
    i2s_read(kAudioPort, input, sizeof(input), &discarded, 0);

  while (captured < kSampleCount) {
    size_t bytes = 0;
    if (i2s_read(kAudioPort, input, sizeof(input), &bytes,
                 pdMS_TO_TICKS(500)) != ESP_OK)
      return false;
    const size_t frames = bytes / 8;
    for (size_t i = 0; i < frames && captured < kSampleCount; ++i) {
      int32_t sample = input[i * 2] >> kMicShift;  // L/R grounded: left slot.
      sample = constrain(sample, -32768, 32767);
      pcm[captured++] = static_cast<int16_t>(sample);
      energy += int64_t(sample) * sample;
      peak = max(peak, abs(sample));
    }
  }
  makeWavHeader(wav, captured);
  rms = captured ? static_cast<uint32_t>(sqrt(double(energy) / captured)) : 0;
  return captured == kSampleCount;
}

String extractTranscript(const String& response) {
  const int marker = response.indexOf("\"text\":");
  if (marker < 0) return "ERROR: invalid Care Hub response";
  int at = response.indexOf('"', marker + 7);
  if (at < 0) return "ERROR: invalid Care Hub response";
  String text;
  for (++at; at < int(response.length()); ++at) {
    const char value = response[at];
    if (value == '"') break;
    if (value == '\\' && at + 1 < int(response.length())) {
      const char escaped = response[++at];
      text += (escaped == 'n' || escaped == 'r' || escaped == 't') ? ' ' : escaped;
    } else {
      text += value;
    }
  }
  return text.length() ? text : "(No speech recognized)";
}

String transcribe(const uint8_t* wav, size_t wavBytes) {
  const size_t encodedCapacity = 4 * ((wavBytes + 2) / 3) + 1;
  auto* encoded = static_cast<unsigned char*>(
      heap_caps_malloc(encodedCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!encoded) return "ERROR: no memory for Base64";

  size_t encodedBytes = 0;
  if (mbedtls_base64_encode(encoded, encodedCapacity, &encodedBytes, wav,
                            wavBytes) != 0) {
    free(encoded);
    return "ERROR: Base64 conversion failed";
  }
  encoded[encodedBytes] = 0;

  String payload;
  if (!payload.reserve(encodedBytes + 48)) {
    free(encoded);
    return "ERROR: no memory for request";
  }
  payload = "{\"audio\":\"data:audio/wav;base64,";
  payload += reinterpret_cast<const char*>(encoded);
  payload += "\"}";
  free(encoded);

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(30000);
  if (!http.begin(client, String(kCareHubUrl) + "/api/transcribe"))
    return "ERROR: invalid Care Hub URL";
  http.addHeader("Content-Type", "application/json");
  const int status = http.POST(payload);
  payload = "";
  const String response = http.getString();
  http.end();

  if (status != 200)
    return "ERROR: Care Hub HTTP " + String(status) + "\n" + response;
  return extractTranscript(response);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  tft.init();
  tft.setRotation(1);
  showMessage("Speech test", "Connecting to saved Wi-Fi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin();
  const uint32_t deadline = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && int32_t(millis() - deadline) < 0)
    delay(200);
  if (WiFi.status() != WL_CONNECTED) {
    showMessage("Wi-Fi failed", "Run Buddy Wi-Fi setup first", TFT_RED);
    return;
  }

  if (!startAudio()) {
    showMessage("Audio failed", "Check SCK 18, WS 21, mic SD 1 and amp DIN 2",
                TFT_RED);
    return;
  }

  auto* wav = static_cast<uint8_t*>(heap_caps_malloc(
      44 + kSampleCount * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!wav) {
    showMessage("Memory failed", "OPI PSRAM was not available", TFT_RED);
    return;
  }

  for (int count = 3; count > 0; --count) {
    showMessage("Get ready", "Recording in " + String(count) + "...");
    delay(700);
  }
  showMessage("Speak now", "Recording for 3 seconds", TFT_GREEN);

  uint32_t rms = 0;
  int32_t peak = 0;
  if (!recordWav(wav, rms, peak)) {
    free(wav);
    showMessage("Record failed", "Check microphone wiring", TFT_RED);
    return;
  }

  showMessage("Recorded", "Repeating in 3...", TFT_GREEN);
  delay(1000);
  showMessage("Recorded", "Repeating in 2...", TFT_GREEN);
  delay(1000);
  showMessage("Recorded", "Repeating in 1...", TFT_GREEN);
  delay(1000);
  showMessage("Repeating...", "Digital peak: " + String(kPlaybackPeak) +
                                  "\nOriginal recording", TFT_CYAN);

  if (!repeatRecording(wav, rms)) {
    free(wav);
    showMessage("Speaker failed", "I2S audio write failed", TFT_RED);
    return;
  }

  showMessage("Transcribing",
              "Microphone level: " + String(rms) + "\nPlease wait...");
  const String result = transcribe(wav, 44 + kSampleCount * 2);
  free(wav);

  Serial.println("Microphone RMS: " + String(rms));
  Serial.println("Recorded peak: " + String(peak));
  Serial.println("Transcript: " + result);
  if (result.startsWith("ERROR:"))
    showMessage("Test failed", result, TFT_RED);
  else
    showMessage("You said:", result + "\n\nPress RESET to try again",
                TFT_GREEN);
}

void loop() {
  // Press RESET to record another phrase.
  delay(1000);
}
