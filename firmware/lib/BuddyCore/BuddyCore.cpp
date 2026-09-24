#include "BuddyCore.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

namespace buddy {
namespace {

float clampUnit(float value) {
  return std::max(-1.0f, std::min(1.0f, value));
}

bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

}  // namespace

void BuddyCore::begin(uint32_t nowMs) {
  snapshot_ = Snapshot{};
  nextGazeMs_ = nowMs + 900;
  nextBlinkMs_ = nowMs + 2200;
}

void BuddyCore::setEventCallback(EventCallback callback, void* context) {
  callback_ = callback;
  callbackContext_ = context;
}

void BuddyCore::setReminders(const std::vector<Reminder>& reminders) {
  reminders_ = reminders;
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Reminder& r) {
    const auto* current = findReminder(r.id);
    return current == nullptr || !current->enabled;
  }), pending_.end());
  snoozed_.erase(std::remove_if(snoozed_.begin(),snoozed_.end(),[&](const Snoozed& s){const auto* r=findReminder(s.reminder.id);return r==nullptr||!r->enabled;}),snoozed_.end());
  if(snapshot_.mode==Mode::Reminder && activeReminder_.id!="demo") {
    const auto* r=findReminder(activeReminder_.id);
    if(!r||!r->enabled){snapshot_.mode=Mode::Idle;snapshot_.activeReminderId.clear();}
  }
}

void BuddyCore::setOnline(bool online) {
  snapshot_.online = online;
}

void BuddyCore::setSosCountdownSeconds(uint8_t seconds) {
  sosCountdownSeconds_ = std::max<uint8_t>(3, std::min<uint8_t>(30, seconds));
}

void BuddyCore::setMissedReminderMinutes(uint16_t minutes) {
  missedReminderMinutes_ = std::max<uint16_t>(1, std::min<uint16_t>(240, minutes));
}

void BuddyCore::tick(uint32_t nowMs, const LocalTime& localTime) {
  localMinute_ = localTime.hour * 60 + localTime.minute;
  updateEyes(nowMs);
  updateSchedule(nowMs, localTime);

  for(auto it=snoozed_.begin();it!=snoozed_.end();) {
    if(deadlineReached(nowMs,it->deadline)){pending_.push_back(it->reminder);it=snoozed_.erase(it);}
    else ++it;
  }

  if (snapshot_.mode == Mode::Reminder && !reminderMissedEmitted_ &&
      deadlineReached(nowMs, reminderMissedDeadlineMs_)) {
    reminderMissedEmitted_ = true;
    emit(EventType::ReminderMissed, snapshot_.activeReminderId,
         "Reminder has not been acknowledged");
  }
  if(snapshot_.mode==Mode::Reminder && reminderMissedEmitted_ && !pending_.empty()) {
    snapshot_.mode=Mode::Idle;
    snapshot_.activeReminderId.clear();
  }
  if(snapshot_.mode==Mode::Reminder && !reminderMissedEmitted_ && repeatsLeft_ && deadlineReached(nowMs,repeatDeadline_)) {
    --repeatsLeft_;repeatDeadline_=nowMs+60000;++snapshot_.speechSequence;
  }

  if (snapshot_.mode == Mode::SosCountdown) {
    if (deadlineReached(nowMs, sosDeadlineMs_)) {
      snapshot_.mode = Mode::SosActive;
      snapshot_.headline = "Emergency alert";
      snapshot_.detail = "Contact your caregiver or local emergency services if you need urgent help. An alert is not a confirmed call.";
      ++snapshot_.speechSequence;
      snapshot_.sosSecondsRemaining = 0;
      emit(EventType::SosEscalated, "sos", "Emergency alert escalated");
    } else {
      const uint32_t remainingMs = sosDeadlineMs_ - nowMs;
      snapshot_.sosSecondsRemaining = static_cast<uint8_t>((remainingMs + 999) / 1000);
    }
  }

  if (transientModeUntilMs_ != 0 && deadlineReached(nowMs, transientModeUntilMs_) &&
      snapshot_.mode != Mode::Reminder && snapshot_.mode != Mode::SosCountdown &&
      snapshot_.mode != Mode::SosActive) {
    transientModeUntilMs_ = 0;
    snapshot_.mode = Mode::Idle;
    snapshot_.headline = "Hello";
    snapshot_.detail = "I'm here when you need me";
  }
  if (snapshot_.mode == Mode::Idle && !pending_.empty()) {
    const auto reminder = pending_.front();
    pending_.erase(pending_.begin());
    activateReminder(reminder, nowMs);
  }
}

void BuddyCore::lookAt(float normalizedX, float normalizedY, uint32_t nowMs) {
  targetGazeX_ = clampUnit(normalizedX);
  targetGazeY_ = clampUnit(normalizedY);
  externalLookUntilMs_ = nowMs + 2200;
}

void BuddyCore::startListening(uint32_t nowMs) {
  if (snapshot_.mode == Mode::SosActive || snapshot_.mode == Mode::SosCountdown || snapshot_.mode == Mode::Reminder) return;
  snapshot_.mode = Mode::Listening;
  snapshot_.headline = "I'm listening";
  snapshot_.detail = "How can I help?";
  transientModeUntilMs_ = nowMs + 12000;
}

void BuddyCore::stopListening(uint32_t nowMs) {
  if (snapshot_.mode != Mode::Listening) return;
  say("Got it", "I'm here whenever you need me", nowMs, 1800);
}

void BuddyCore::say(const std::string& headline, const std::string& detail,
                    uint32_t nowMs, uint32_t durationMs) {
  if (snapshot_.mode == Mode::SosActive || snapshot_.mode == Mode::SosCountdown || snapshot_.mode == Mode::Reminder) return;
  snapshot_.mode = Mode::Speaking;
  snapshot_.speechAllowed = true;
  snapshot_.headline = headline;
  snapshot_.detail = detail;
  transientModeUntilMs_ = nowMs + durationMs;
  ++snapshot_.speechSequence;
}

void BuddyCore::triggerReminder(const Reminder& reminder, uint32_t nowMs) {
  if (snapshot_.mode == Mode::SosCountdown || snapshot_.mode == Mode::SosActive || snapshot_.mode == Mode::Reminder) { pending_.push_back(reminder); return; }
  activateReminder(reminder, nowMs);
}

void BuddyCore::acknowledge(uint32_t nowMs) {
  if (snapshot_.mode == Mode::Reminder) {
    const std::string id = snapshot_.activeReminderId;
    snapshot_.activeReminderId.clear();
    emit(EventType::ReminderAcknowledged, id, "Reminder acknowledged");
    snapshot_.mode = Mode::Idle;
    say("All done", "Thank you for letting me know", nowMs, 2500);
    return;
  }
}

void BuddyCore::snooze(uint32_t nowMs, uint16_t minutes) {
  if (snapshot_.mode != Mode::Reminder) return;
  const Reminder* reminder = &activeReminder_;
  const uint16_t delayMinutes = minutes == 0 ? reminder->snoozeMinutes : minutes;
  snoozed_.push_back({*reminder,static_cast<uint32_t>(nowMs+static_cast<uint32_t>(std::min<uint16_t>(delayMinutes,240))*60000UL)});
  snapshot_.activeReminderId.clear();
  emit(EventType::ReminderSnoozed, reminder->id, "Reminder snoozed");
  snapshot_.mode = Mode::Idle;
  say("Okay", "I'll remind you again later", nowMs, 2500);
}

void BuddyCore::startSos(uint32_t nowMs) {
  if (snapshot_.mode == Mode::SosActive || snapshot_.mode == Mode::SosCountdown) return;
  if (snapshot_.mode == Mode::Reminder) pending_.insert(pending_.begin(), activeReminder_);
  snapshot_.activeReminderId.clear();
  snapshot_.mode = Mode::SosCountdown;
  snapshot_.speechAllowed = true;
  snapshot_.headline = "Calling for help";
  snapshot_.detail = "Say hey " + callName_ + " cancel SOS if this was accidental";
  snapshot_.sosSecondsRemaining = sosCountdownSeconds_;
  sosDeadlineMs_ = nowMs + static_cast<uint32_t>(sosCountdownSeconds_) * 1000UL;
  transientModeUntilMs_ = 0;
  emit(EventType::SosStarted, "sos", "Emergency countdown started");
  ++snapshot_.speechSequence;
}

void BuddyCore::cancelSos(uint32_t nowMs) {
  if (snapshot_.mode != Mode::SosCountdown && snapshot_.mode != Mode::SosActive) return;
  snapshot_.sosSecondsRemaining = 0;
  emit(EventType::SosCancelled, "sos", "Emergency alert cancelled");
  snapshot_.mode = Mode::Idle;
  say("Alert cancelled", "The alert has been cancelled", nowMs, 2500);
}

void BuddyCore::setCheckIn(const std::string& mood, uint32_t nowMs) {
  emit(EventType::CheckIn, mood, "Wellbeing check-in recorded");
  say("Thank you", "I've saved how you're feeling", nowMs, 2500);
}

void BuddyCore::emit(EventType type, const std::string& id,
                     const std::string& message) {
  if (callback_ == nullptr) return;
  callback_(Event{type, id, message}, callbackContext_);
}

void BuddyCore::updateEyes(uint32_t nowMs) {
  if (deadlineReached(nowMs, nextBlinkMs_) && blinkStartMs_ == 0) {
    blinkStartMs_ = nowMs;
  }

  if (blinkStartMs_ != 0) {
    const uint32_t elapsed = nowMs - blinkStartMs_;
    if (elapsed < 90) {
      snapshot_.eyeOpen = 1.0f - static_cast<float>(elapsed) / 90.0f;
    } else if (elapsed < 180) {
      snapshot_.eyeOpen = static_cast<float>(elapsed - 90) / 90.0f;
    } else {
      snapshot_.eyeOpen = 1.0f;
      blinkStartMs_ = 0;
      nextBlinkMs_ = nowMs + 2200 + static_cast<uint32_t>(randomUnit() * 3000.0f);
    }
  }

  if (deadlineReached(nowMs, externalLookUntilMs_) && deadlineReached(nowMs, nextGazeMs_)) {
    targetGazeX_ = randomUnit() * 1.4f - 0.7f;
    targetGazeY_ = randomUnit() * 0.8f - 0.4f;
    nextGazeMs_ = nowMs + 1400 + static_cast<uint32_t>(randomUnit() * 2200.0f);
  }

  const float easing = 0.12f;
  snapshot_.gazeX += (targetGazeX_ - snapshot_.gazeX) * easing;
  snapshot_.gazeY += (targetGazeY_ - snapshot_.gazeY) * easing;
}

void BuddyCore::updateSchedule(uint32_t nowMs, const LocalTime& localTime) {
  if(localTime.year<2024 || localTime.month<1 || localTime.month>12 || localTime.day<1 || localTime.day>31 || localTime.weekday>6)return;
  const uint32_t today = dateKey(localTime);
  const int minute = localTime.hour * 60 + localTime.minute;
  for (const Reminder& reminder : reminders_) {
    // Recover reminders up to 20 minutes late, never advise a catch-up dose.
    const int delay = minute - (reminder.hour * 60 + reminder.minute);
    if (!occursOn(reminder, localTime) || delay < 0 || delay > 20) continue;
    const bool fired = std::any_of(triggered_.begin(), triggered_.end(), [&](const Occurrence& o) { return o.id == reminder.id && o.date == today; });
    if (fired) continue;
    restoreOccurrence(reminder.id, today);
    pending_.push_back(reminder);
  }
  if (snapshot_.mode == Mode::Idle && !pending_.empty()) {
    const auto reminder = pending_.front(); pending_.erase(pending_.begin());
    activateReminder(reminder, nowMs);
  }
}

void BuddyCore::activateReminder(const Reminder& reminder, uint32_t nowMs) {
  snapshot_.mode = Mode::Reminder;
  snapshot_.headline = reminder.title;
  snapshot_.detail = reminder.detail.empty() ? "It's time" : reminder.detail;
  snapshot_.activeReminderId = reminder.id;
  activeReminder_ = reminder;
  repeatsLeft_=repeatCount_-1;repeatDeadline_=nowMs+60000;
  const bool quiet = quietStart_ != quietEnd_ && (quietStart_ < quietEnd_
    ? localMinute_ >= quietStart_ && localMinute_ < quietEnd_
    : localMinute_ >= quietStart_ || localMinute_ < quietEnd_);
  snapshot_.speechAllowed = !quiet || reminder.kind == "medicine";
  ++snapshot_.speechSequence;
  reminderMissedEmitted_ = false;
  reminderMissedDeadlineMs_ = nowMs + static_cast<uint32_t>(missedReminderMinutes_) * 60000UL;
  transientModeUntilMs_ = 0;
  emit(EventType::ReminderDue, reminder.id, reminder.title);
}

const Reminder* BuddyCore::findReminder(const std::string& id) const {
  for (const Reminder& reminder : reminders_) {
    if (reminder.id == id) return &reminder;
  }
  return nullptr;
}

float BuddyCore::randomUnit() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return static_cast<float>((rng_ >> 8) & 0x00FFFFFFu) / 16777215.0f;
}

const char* BuddyCore::modeName(Mode mode) {
  switch (mode) {
    case Mode::Idle: return "idle";
    case Mode::Listening: return "listening";
    case Mode::Speaking: return "speaking";
    case Mode::Reminder: return "reminder";
    case Mode::SosCountdown: return "sos_countdown";
    case Mode::SosActive: return "sos_active";
    case Mode::Offline: return "offline";
  }
  return "unknown";
}

const char* BuddyCore::eventName(EventType event) {
  switch (event) {
    case EventType::None: return "none";
    case EventType::ReminderDue: return "reminder_due";
    case EventType::ReminderAcknowledged: return "reminder_acknowledged";
    case EventType::ReminderSnoozed: return "reminder_snoozed";
    case EventType::ReminderMissed: return "reminder_missed";
    case EventType::SosStarted: return "sos_started";
    case EventType::SosCancelled: return "sos_cancelled";
    case EventType::SosEscalated: return "sos_escalated";
    case EventType::CheckIn: return "check_in";
    case EventType::Hydration: return "hydration";
    case EventType::WakeDetected: return "wake_detected";
    case EventType::CallControl: return "call_control";
  }
  return "unknown";
}

uint32_t BuddyCore::dateKey(const LocalTime& time) {
  return uint32_t(time.year) * 10000 + time.month * 100 + time.day;
}

bool BuddyCore::occursOn(const Reminder& reminder, const LocalTime& time) {
  const auto date = dateKey(time);
  return reminder.enabled && time.weekday < 7 && (reminder.daysMask & (1u << time.weekday)) &&
    (!reminder.startDate || date >= reminder.startDate) && (!reminder.endDate || date <= reminder.endDate);
}

void BuddyCore::restoreOccurrence(const std::string& id, uint32_t date) {
  if (std::any_of(triggered_.begin(), triggered_.end(), [&](const Occurrence& o) { return o.id == id && o.date == date; })) return;
  triggered_.push_back({id, date});
  if (triggered_.size() > 256) triggered_.erase(triggered_.begin());
}

namespace {
std::string words(const std::string& input) {
  std::string output;
  for (unsigned char ch : input) {
    if (std::isalnum(ch) || ch >= 128) output += static_cast<char>(std::tolower(ch));
    else if (!output.empty() && output.back() != ' ') output += ' ';
  }
  while (!output.empty() && output.back() == ' ') output.pop_back();
  return output;
}
bool contains(const std::string& input, const std::string& phrase) {
  return (" " + input + " ").find(" " + phrase + " ") != std::string::npos;
}
}

void BuddyCore::setCallName(const std::string& name) {
  const auto normalized = words(name);
  callName_ = normalized.empty() ? "buddy" : normalized.substr(0, 24);
  awakeUntilMs_ = 0;
}

void BuddyCore::setQuietHours(uint16_t startMinute, uint16_t endMinute) {
  quietStart_ = startMinute % 1440;
  quietEnd_ = endMinute % 1440;
}

std::string BuddyCore::agenda(const LocalTime& time) const {
  std::vector<Reminder> today;
  for (const auto& r : reminders_) if (occursOn(r, time)) today.push_back(r);
  std::sort(today.begin(), today.end(), [](const Reminder& a, const Reminder& b) { return a.hour * 60 + a.minute < b.hour * 60 + b.minute; });
  if (today.empty()) return "There are no reminders scheduled for today.";
  std::string reply = "Today: ";
  for (const auto& r : today) {
    char clock[10]; std::snprintf(clock, sizeof(clock), "%02u:%02u", r.hour, r.minute);
    reply += r.title + " at " + clock + ". ";
    if (reply.size() > 380) { reply += "The full day is in your calendar."; break; }
  }
  return reply;
}

std::string BuddyCore::hear(const std::string& text, uint32_t nowMs, const LocalTime& time) {
  assistantQuery_.clear();
  auto input = words(text);
  if (input.empty()) return "ignored";
  const std::string phrase = "hey " + callName_;
  const auto wake = (" " + input + " ").find(" " + phrase + " ");
  const bool shortWake = input == "hey";
  if (wake != std::string::npos || shortWake) {
    input = shortWake ? "" : input.substr(std::min(input.size(), wake + phrase.size()));
    while (!input.empty() && input.front() == ' ') input.erase(input.begin());
    awakeUntilMs_ = nowMs + 12000;
    emit(EventType::WakeDetected, callName_, "Wake phrase heard");
  } else if (awakeUntilMs_ == 0 || deadlineReached(nowMs, awakeUntilMs_)) return "ignored";
  if (input.empty()) { startListening(nowMs); return "awake"; }
  awakeUntilMs_ = 0;
  if (input == "cancel sos" || input == "cancel the alert" || input == "cancel emergency") {
    cancelSos(nowMs); return "handled";
  }
  if (input == "help" || input == "help me" || input == "sos" || input == "emergency" || input == "call for help" || contains(input, "i fell")) {
    startSos(nowMs); return "handled";
  }
  if(input=="end call"||input=="hang up"||input=="stop camera"||input=="decline call") {
    callControl(input=="decline call"?"decline":"end");return "handled";
  }
  if(snapshot_.mode!=Mode::SosActive&&snapshot_.mode!=Mode::SosCountdown) {
    if(input=="answer call"||input=="accept call"||input=="allow camera") {callControl("accept");return "handled";}
    if(input.compare(0,5,"call ")==0&&input.size()>5) {callControl("start",input.substr(5));return "handled";}
  }
  if (input == "done" || input == "i have taken it" || input == "i ve taken it" || input == "taken" || input == "all done" || input == "yes") {
    if (snapshot_.mode == Mode::Reminder) acknowledge(nowMs);
    else say("No active reminder", "There is no reminder waiting for confirmation.", nowMs);
    return "handled";
  }
  if (input == "snooze" || input == "remind me later" || input == "remind me in ten minutes") {
    snooze(nowMs, 10); return "handled";
  }
  if (input == "repeat" || input == "say that again") { ++snapshot_.speechSequence; return "handled"; }
  if (snapshot_.mode == Mode::SosActive || snapshot_.mode == Mode::SosCountdown) return "handled";
  if (input == "i drank water" || input == "had some water") {
    emit(EventType::Hydration, "water", "Water check-in recorded");
    if(snapshot_.mode==Mode::Reminder && activeReminder_.kind=="hydration")acknowledge(nowMs);
    say("Water logged", "Thank you for letting me know.", nowMs); return "handled";
  }
  if (input == "i feel good" || input == "i feel okay" || input == "i feel lonely" || input == "i feel unwell") {
    if(snapshot_.mode==Mode::Reminder && activeReminder_.kind=="wellbeing")acknowledge(nowMs);
    setCheckIn(input.substr(7), nowMs); return "handled";
  }
  if (snapshot_.mode == Mode::Reminder) return "handled";
  if (contains(input, "today") || contains(input, "calendar") || contains(input, "schedule") || contains(input, "next reminder")) {
    say("Your day", agenda(time), nowMs, 15000); return "handled";
  }
  if (contains(input, "time") || contains(input, "date")) {
    char reply[80]; std::snprintf(reply, sizeof(reply), "It is %02u:%02u on %02u/%02u/%04u.", time.hour, time.minute, time.day, time.month, time.year);
    say("Clock", reply, nowMs, 6000); return "handled";
  }
  if (input == "what can you do") {
    say("I'm here to help", "Ask about your day, say I have taken it, remind me later, I drank water, or call for help.", nowMs, 10000); return "handled";
  }
  assistantQuery_ = input;
  startListening(nowMs);
  return "assistant";
}

}  // namespace buddy
