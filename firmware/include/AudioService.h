#pragma once

#include <Arduino.h>

class AudioService {
 public:
  void begin();
  void configure(const String& voice, uint8_t speed, uint8_t pitch,
                 uint8_t volume);
  void speak(const String& text);
  void chimeReminder();
  void chimeSos();

 private:
  String voice_ = "bella";
  uint8_t speed_ = 72;
  uint8_t pitch_ = 64;
  uint8_t volume_ = 80;
};
