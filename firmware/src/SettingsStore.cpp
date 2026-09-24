#include "SettingsStore.h"

#include <LittleFS.h>

namespace {
constexpr const char* kSettingsPath = "/buddy-settings.json";
bool validDate(const String& value) {
  if(value.isEmpty())return true;
  if(value.length()!=10||value[4]!='-'||value[7]!='-')return false;
  for(int i=0;i<10;i++)if(i!=4&&i!=7&&!isDigit(value[i]))return false;
  const int y=value.substring(0,4).toInt(),m=value.substring(5,7).toInt(),d=value.substring(8).toInt();
  if(y<2024||y>2100||m<1||m>12)return false;
  const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
  return d>=1&&d<=days[m-1]+(m==2&&y%4==0&&(y%100!=0||y%400==0));
}

uint8_t parseDays(JsonArrayConst days) {
  uint8_t mask = 0;
  for (JsonVariantConst day : days) {
    const int value = day.as<int>();
    if (value >= 0 && value <= 6) mask |= static_cast<uint8_t>(1u << value);
  }
  return mask;
}
}  // namespace

bool SettingsStore::begin() {
  if (!LittleFS.begin(true)) return false;
  if (!LittleFS.exists(kSettingsPath)) {
    restoreDefaults();
    return save();
  }

  File file = LittleFS.open(kSettingsPath, "r");
  const DeserializationError error = deserializeJson(document_, file);
  file.close();
  String validationError;
  if (error || !validate(validationError)) {
    Serial.printf("Settings invalid; restored defaults: %s\n",
                  error ? error.c_str() : validationError.c_str());
    restoreDefaults();
    return save();
  }
  return true;
}

bool SettingsStore::replaceFromJson(const String& json, String& error) {
  DynamicJsonDocument incoming(49152);
  const DeserializationError parseError = deserializeJson(incoming, json);
  if (parseError) {
    error = String("Invalid JSON: ") + parseError.c_str();
    return false;
  }
  DynamicJsonDocument previous(49152);
  previous.set(document_);
  document_.set(incoming);
  if (!validate(error)) {
    document_.set(previous);
    return false;
  }
  if(!save()){document_.set(previous);error="Could not persist settings";return false;}
  return true;
}

bool SettingsStore::save() {
  File file = LittleFS.open("/buddy-settings.tmp", "w");
  if (!file) return false;
  const size_t written = serializeJson(document_, file);
  file.close();
  return written > 0 && LittleFS.rename("/buddy-settings.tmp",kSettingsPath);
}

void SettingsStore::restoreDefaults() {
  document_.clear();
  document_["version"] = 1;

  JsonObject person = document_.createNestedObject("person");
  person["name"] = "Friend";
  person["language"] = "en-IN";
  person["pronouns"] = "";

  JsonObject voice = document_.createNestedObject("voice");
  voice["voiceId"] = "bella";
  voice["rate"] = 72;
  voice["pitch"] = 64;
  voice["volume"] = 82;
  voice["repeatCount"] = 2;

  JsonObject display = document_.createNestedObject("display");
  display["brightness"] = 220;
  display["highContrast"] = true;
  display["fontScale"] = "large";
  display["eyesEnabled"] = true;

  JsonObject safety = document_.createNestedObject("safety");
  safety["sosCountdownSeconds"] = 8;
  safety["webhookUrl"] = "";
  safety["missedReminderMinutes"] = 20;
  safety["quietStart"] = "22:00";
  safety["quietEnd"] = "07:00";

  JsonObject device = document_.createNestedObject("device");
  device["timezonePosix"] = "IST-5:30";
  device["deviceName"] = "Living Room Buddy";
  device["callName"] = "Buddy";
  device["timezone"] = "Asia/Kolkata";
  device["voiceServerUrl"] = "";
  display["orientation"] = "landscape";
  display["eyeColor"] = 20127;

  // The paired care circle supplies real names, contacts and reminders.
  document_.createNestedArray("contacts");
  document_.createNestedArray("reminders");

  JsonObject preferences = document_.createNestedObject("preferences");
  preferences["dailyGreeting"] = true;
  preferences["hydrationPrompts"] = true;
  preferences["familyMessages"] = true;
  preferences["privacyMode"] = "wake-phrase";
}

String SettingsStore::toJson(bool pretty) const {
  String output;
  if (pretty) serializeJsonPretty(document_, output);
  else serializeJson(document_, output);
  return output;
}

String SettingsStore::personName() const {
  return document_["person"]["name"] | "Friend";
}

String SettingsStore::timezonePosix() const {
  return document_["device"]["timezonePosix"] | "UTC0";
}

uint8_t SettingsStore::brightness() const {
  return document_["display"]["brightness"] | 220;
}

String SettingsStore::voiceId() const {
  return document_["voice"]["voiceId"] | "bella";
}
String SettingsStore::callName() const { return document_["device"]["callName"] | "Buddy"; }
uint8_t SettingsStore::repeatCount() const { return document_["voice"]["repeatCount"]|2; }
String SettingsStore::voiceServerUrl() const { return document_["device"]["voiceServerUrl"] | ""; }
String SettingsStore::language() const { return document_["person"]["language"] | "en-IN"; }
uint16_t SettingsStore::quietStart() const { const String t=document_["safety"]["quietStart"]|"22:00";return t.substring(0,2).toInt()*60+t.substring(3).toInt(); }
uint16_t SettingsStore::quietEnd() const { const String t=document_["safety"]["quietEnd"]|"07:00";return t.substring(0,2).toInt()*60+t.substring(3).toInt(); }
bool SettingsStore::portrait() const { return String(document_["display"]["orientation"] | "landscape") == "portrait"; }
uint16_t SettingsStore::eyeColor() const { return document_["display"]["eyeColor"] | 20127; }

uint8_t SettingsStore::voiceSpeed() const {
  return document_["voice"]["rate"] | 72;
}

uint8_t SettingsStore::voicePitch() const {
  return document_["voice"]["pitch"] | 64;
}

uint8_t SettingsStore::voiceVolume() const {
  return document_["voice"]["volume"] | 82;
}

uint8_t SettingsStore::sosCountdownSeconds() const {
  return document_["safety"]["sosCountdownSeconds"] | 8;
}

uint16_t SettingsStore::missedReminderMinutes() const {
  return document_["safety"]["missedReminderMinutes"] | 20;
}

String SettingsStore::webhookUrl() const {
  return document_["safety"]["webhookUrl"] | "";
}

void SettingsStore::addAlertContacts(JsonArray target, bool missedReminder) const {
  for (JsonObjectConst contact : document_["contacts"].as<JsonArrayConst>()) {
    const bool selected = missedReminder
        ? (contact["receivesMissedReminders"] | false)
        : (contact["receivesSos"] | false);
    if (!selected) continue;
    JsonObject output = target.createNestedObject();
    output["id"] = contact["id"] | "";
    output["name"] = contact["name"] | "";
    output["relationship"] = contact["relationship"] | "";
    output["phone"] = contact["phone"] | "";
  }
}

std::vector<buddy::Reminder> SettingsStore::reminders() const {
  std::vector<buddy::Reminder> result;
  JsonArrayConst items = document_["reminders"].as<JsonArrayConst>();
  result.reserve(items.size());
  for (JsonObjectConst item : items) {
    const String time = item["time"] | "09:00";
    buddy::Reminder reminder;
    reminder.id = item["id"] | "reminder";
    reminder.title = item["title"] | "Reminder";
    reminder.detail = item["detail"] | "It's time";
    reminder.kind = item["kind"] | "activity";
    reminder.hour = time.substring(0, 2).toInt();
    reminder.minute = time.substring(3, 5).toInt();
    reminder.daysMask = parseDays(item["days"].as<JsonArrayConst>());
    reminder.enabled = item["enabled"] | true;
    reminder.snoozeMinutes = item["snoozeMinutes"] | 10;
    String start=item["startDate"]|""; start.replace("-",""); reminder.startDate=start.toInt();
    String end=item["endDate"]|""; end.replace("-",""); reminder.endDate=end.toInt();
    result.push_back(reminder);
  }
  return result;
}

bool SettingsStore::validate(String& error) const {
  if (!document_.is<JsonObject>()) {
    error = "Settings must be a JSON object";
    return false;
  }
  if (!document_["person"].is<JsonObject>() ||
      String(document_["person"]["name"] | "").length() == 0) {
    error = "A person name is required";
    return false;
  }
  if (!document_["reminders"].is<JsonArray>() ||
      !document_["contacts"].is<JsonArray>()) {
    error = "Contacts and reminders must be arrays";
    return false;
  }
  if (document_["reminders"].size() > 64 || document_["contacts"].size() > 12) {
    error = "Maximum: 64 reminders and 12 contacts";
    return false;
  }
  const String name=callName(),orientation=document_["display"]["orientation"]|"landscape";
  if(name.length()<2||name.length()>24){error="Call name must have 2-24 letters";return false;}
  for(unsigned int i=0;i<name.length();i++)if(!isAlpha(name[i])&&name[i]!=' '){error="Use letters for the call name";return false;}
  if(orientation!="landscape"&&orientation!="portrait"){error="Invalid orientation";return false;}
  for(const char* key:{"quietStart","quietEnd"}) {
    const String time=document_["safety"][key]|"00:00";
    if(time.length()!=5||time[2]!=':'||!isDigit(time[0])||!isDigit(time[1])||!isDigit(time[3])||!isDigit(time[4])||time.substring(0,2).toInt()>23||time.substring(3).toInt()>59){error="Invalid quiet hours";return false;}
  }
  std::vector<String> ids;
  for (JsonObjectConst reminder : document_["reminders"].as<JsonArrayConst>()) {
    const String id=reminder["id"]|"",title=reminder["title"]|"",start=reminder["startDate"]|"",end=reminder["endDate"]|"";
    if(id.isEmpty()||title.isEmpty()||title.length()>120||!validDate(start)||!validDate(end)||(!start.isEmpty()&&!end.isEmpty()&&end<start)){error="Invalid reminder ID, title or dates";return false;}
    for(const auto& previous:ids)if(previous==id){error="Duplicate reminder ID";return false;}ids.push_back(id);
    for(JsonVariantConst day:reminder["days"].as<JsonArrayConst>())if(!day.is<int>()||day.as<int>()<0||day.as<int>()>6){error="Invalid weekday";return false;}
    const String time=reminder["time"]|"";
    if(time.length()!=5 || time[2]!=':' || !isDigit(time[0]) || !isDigit(time[1]) || !isDigit(time[3]) || !isDigit(time[4]) || time.substring(0,2).toInt()>23 || time.substring(3).toInt()>59 || parseDays(reminder["days"].as<JsonArrayConst>())==0) {error="Invalid reminder time or weekdays";return false;}
  }
  return true;
}
