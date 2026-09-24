#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace buddy {

enum class Mode : uint8_t {
  Idle,
  Listening,
  Speaking,
  Reminder,
  SosCountdown,
  SosActive,
  Offline
};

enum class EventType : uint8_t {
  None,
  ReminderDue,
  ReminderAcknowledged,
  ReminderSnoozed,
  ReminderMissed,
  SosStarted,
  SosCancelled,
  SosEscalated,
  CheckIn,
  Hydration,
  WakeDetected,
  CallControl
};

struct LocalTime {
  uint16_t year = 2026;
  uint8_t month = 1;
  uint8_t day = 1;
  uint8_t weekday = 0;  // 0 = Sunday
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
};

struct Reminder {
  std::string id;
  std::string title;
  std::string detail;
  std::string kind = "activity";
  uint8_t hour = 9;
  uint8_t minute = 0;
  uint8_t daysMask = 0x7F;  // bit 0 = Sunday
  bool enabled = true;
  uint16_t snoozeMinutes = 10;
  uint32_t startDate = 0;  // YYYYMMDD, inclusive; 0 = no boundary
  uint32_t endDate = 0;
};

struct Event {
  EventType type = EventType::None;
  std::string id;
  std::string message;
};

struct Snapshot {
  Mode mode = Mode::Idle;
  std::string headline = "Hello";
  std::string detail = "I'm here when you need me";
  std::string activeReminderId;
  float gazeX = 0.0f;
  float gazeY = 0.0f;
  float eyeOpen = 1.0f;
  uint8_t sosSecondsRemaining = 0;
  bool online = false;
  uint32_t speechSequence = 0;
  bool speechAllowed = true;
  bool cameraActive = false;
};

class BuddyCore {
 public:
  using EventCallback = void (*)(const Event&, void* context);

  void begin(uint32_t nowMs);
  void tick(uint32_t nowMs, const LocalTime& localTime);
  void setEventCallback(EventCallback callback, void* context);
  void setReminders(const std::vector<Reminder>& reminders);
  void setOnline(bool online);
  void setSosCountdownSeconds(uint8_t seconds);
  void setMissedReminderMinutes(uint16_t minutes);

  void lookAt(float normalizedX, float normalizedY, uint32_t nowMs);
  void startListening(uint32_t nowMs);
  void stopListening(uint32_t nowMs);
  void say(const std::string& headline, const std::string& detail,
           uint32_t nowMs, uint32_t durationMs = 3500);
  void triggerReminder(const Reminder& reminder, uint32_t nowMs);
  void acknowledge(uint32_t nowMs);
  void snooze(uint32_t nowMs, uint16_t minutes = 0);
  void startSos(uint32_t nowMs);
  void cancelSos(uint32_t nowMs);
  void setCheckIn(const std::string& mood, uint32_t nowMs);
  void setCameraActive(bool active) { snapshot_.cameraActive=active; }
  void callControl(const std::string& action,const std::string& target="") { emit(EventType::CallControl,action,target); }
  void setCallName(const std::string& name);
  void setQuietHours(uint16_t startMinute, uint16_t endMinute);
  void setReminderRepeats(uint8_t count) { repeatCount_ = count<1?1:count>3?3:count; }
  // Returns ignored, handled, awake, or assistant. Only text following the
  // configured wake phrase (or the short follow-up window) reaches commands.
  std::string hear(const std::string& text, uint32_t nowMs, const LocalTime& time);
  const std::string& assistantQuery() const { return assistantQuery_; }
  std::string agenda(const LocalTime& time) const;
  static uint32_t dateKey(const LocalTime& time);
  static bool occursOn(const Reminder& reminder, const LocalTime& time);
  void restoreOccurrence(const std::string& id, uint32_t date);

  const Snapshot& snapshot() const { return snapshot_; }
  const std::vector<Reminder>& reminders() const { return reminders_; }

  static const char* modeName(Mode mode);
  static const char* eventName(EventType event);

 private:
  void emit(EventType type, const std::string& id,
            const std::string& message);
  void updateEyes(uint32_t nowMs);
  void updateSchedule(uint32_t nowMs, const LocalTime& localTime);
  void activateReminder(const Reminder& reminder, uint32_t nowMs);
  const Reminder* findReminder(const std::string& id) const;
  float randomUnit();

  Snapshot snapshot_;
  std::vector<Reminder> reminders_;
  EventCallback callback_ = nullptr;
  void* callbackContext_ = nullptr;

  uint32_t rng_ = 0xBADD1E5u;
  uint32_t nextGazeMs_ = 0;
  uint32_t nextBlinkMs_ = 0;
  uint32_t blinkStartMs_ = 0;
  uint32_t externalLookUntilMs_ = 0;
  uint32_t transientModeUntilMs_ = 0;
  uint32_t sosDeadlineMs_ = 0;
  uint32_t reminderMissedDeadlineMs_ = 0;
  uint8_t sosCountdownSeconds_ = 8;
  uint16_t missedReminderMinutes_ = 20;
  bool reminderMissedEmitted_ = false;

  float targetGazeX_ = 0.0f;
  float targetGazeY_ = 0.0f;
  struct Snoozed { Reminder reminder; uint32_t deadline; };
  std::vector<Snoozed> snoozed_;
  Reminder activeReminder_;
  std::vector<Reminder> pending_;
  struct Occurrence { std::string id; uint32_t date; };
  std::vector<Occurrence> triggered_;
  std::string callName_ = "buddy";
  std::string assistantQuery_;
  uint32_t awakeUntilMs_ = 0;
  uint16_t quietStart_ = 1320, quietEnd_ = 420, localMinute_ = 720;
  uint8_t repeatCount_=2, repeatsLeft_=0;
  uint32_t repeatDeadline_=0;
};

}  // namespace buddy
