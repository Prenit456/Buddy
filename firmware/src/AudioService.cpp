#include "AudioService.h"

#include "HardwareConfig.h"

#if BUDDY_ENABLE_SAM
#include <AudioOutputI2S.h>
#include <ESP8266SAM.h>
namespace {
AudioOutputI2S* output = nullptr;
}
#endif

void AudioService::begin() {
#if BUDDY_ENABLE_SAM
  output = new AudioOutputI2S();
  output->SetPinout(buddy_hw::kI2sBclkPin, buddy_hw::kI2sLrcPin,
                    buddy_hw::kI2sDataOutPin);
  output->SetGain(0.65f);
  output->begin();
#else
  if (buddy_hw::kBuzzerPin >= 0) pinMode(buddy_hw::kBuzzerPin, OUTPUT);
#endif
}

void AudioService::configure(const String& voice, uint8_t speed, uint8_t pitch,
                             uint8_t volume) {
  voice_ = voice;
  speed_ = constrain(speed, 40, 120);
  pitch_ = constrain(pitch, 30, 100);
  volume_ = constrain(volume, 0, 100);
#if BUDDY_ENABLE_SAM
  if (output != nullptr) output->SetGain(static_cast<float>(volume_) / 100.0f);
#endif
}

void AudioService::speak(const String& text) {
  Serial.printf("[voice=%s speed=%u pitch=%u] %s\n", voice_.c_str(), speed_,
                pitch_, text.c_str());
#if BUDDY_ENABLE_SAM
  if (output == nullptr) return;
  ESP8266SAM sam;
  // SAM is an emergency offline fallback. Natural named profiles are rendered
  // by the companion TTS proxy when the network audio adapter is enabled.
  sam.SetVoice(ESP8266SAM::VOICE_SAM);
  sam.SetSpeed(speed_);
  sam.SetPitch(pitch_);
  sam.Say(output, text.c_str());
  output->stop();
  output->begin();
#else
  // The base environment intentionally remains hardware-neutral. It gives a
  // short cue and logs speech text. Build buddy-s3-sam for offline speech.
  if (buddy_hw::kBuzzerPin >= 0) tone(buddy_hw::kBuzzerPin, 740, 90);
#endif
}

void AudioService::chimeReminder() {
#if BUDDY_ENABLE_SAM
  speak("You have a reminder.");
#else
  if (buddy_hw::kBuzzerPin < 0) return;
  tone(buddy_hw::kBuzzerPin, 660, 130);
  delay(150);
  tone(buddy_hw::kBuzzerPin, 880, 180);
#endif
}

void AudioService::chimeSos() {
#if BUDDY_ENABLE_SAM
  speak("Calling for help. Hold the top button again to cancel.");
#else
  if (buddy_hw::kBuzzerPin >= 0) tone(buddy_hw::kBuzzerPin, 980, 350);
#endif
}
