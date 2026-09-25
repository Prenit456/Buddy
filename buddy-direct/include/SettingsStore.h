#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BuddyCore.h>

class SettingsStore {
 public:
  bool begin();
  bool replaceFromJson(const String& json, String& error);
  bool save();
  void restoreDefaults();

  String toJson(bool pretty = false) const;
  String personName() const;
  String timezonePosix() const;
  uint8_t brightness() const;
  String voiceId() const;
  String callName() const;
  String voiceServerUrl() const;
  String language() const;
  uint16_t quietStart() const;
  uint16_t quietEnd() const;
  uint8_t repeatCount() const;
  bool portrait() const;
  uint16_t eyeColor() const;
  uint8_t voiceSpeed() const;
  uint8_t voicePitch() const;
  uint8_t voiceVolume() const;
  uint8_t sosCountdownSeconds() const;
  uint16_t missedReminderMinutes() const;
  String webhookUrl() const;
  void addAlertContacts(JsonArray target, bool missedReminder) const;
  std::vector<buddy::Reminder> reminders() const;

 private:
  bool validate(String& error) const;
  DynamicJsonDocument document_{49152};
};
