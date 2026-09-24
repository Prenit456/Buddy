#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <sys/time.h>
#include <map>
#include <algorithm>

#include <BuddyCore.h>

#include "AudioService.h"
#include "DisplayUI.h"
#include "HardwareConfig.h"
#include "HardwareValidation.h"
#include "SettingsStore.h"
#include "VoiceGateway.h"
#include "HubSync.h"

namespace {

buddy::BuddyCore core;
DisplayUI display;
AudioService audio;
VoiceGateway voice;
HubSync hub;
SettingsStore settings;
WebServer server(80);

struct LogEntry {
  uint32_t atMs;
  String type;
  String message;
};

std::vector<LogEntry> eventLog;
bool sosWasDown = false;
uint32_t sosDownSince = 0;
bool sosLongPressHandled = false;
int currentBatteryPercent = buddy_hw::kBatteryFallbackPercent;
uint32_t nextBatterySampleMs = 0;
buddy::LocalTime currentLocalTime();
uint32_t restoredDate = 0;
uint32_t spokenSequence = 0;
uint32_t lastCoreTick = 0;
struct AlertJob { String url,body; };
QueueHandle_t alertJobs=nullptr,alertResults=nullptr;

void alertWorker(void*) {
  for(;;){
    AlertJob* job=nullptr;if(xQueueReceive(alertJobs,&job,portMAX_DELAY)!=pdTRUE)continue;
    HTTPClient http;http.setConnectTimeout(4000);http.setTimeout(5000);int response=-1;
    if(job->url.startsWith("https://")) {
      WiFiClientSecure client;client.setInsecure(); // Prototype: production must validate the webhook CA.
      if(http.begin(client,job->url)){http.addHeader("Content-Type","application/json");response=http.POST(job->body);http.end();}
    } else {
      WiFiClient client;if(http.begin(client,job->url)){http.addHeader("Content-Type","application/json");response=http.POST(job->body);http.end();}
    }
    delete job;xQueueSend(alertResults,&response,0);
  }
}

int readBatteryPercent() {
  if (buddy_hw::kBatterySensePin < 0) return buddy_hw::kBatteryFallbackPercent;
  const float measuredVolts = analogReadMilliVolts(buddy_hw::kBatterySensePin) /
                              1000.0f * buddy_hw::kBatteryDividerRatio;
  float fraction = (measuredVolts - buddy_hw::kBatteryEmptyVoltage) /
                   (buddy_hw::kBatteryFullVoltage - buddy_hw::kBatteryEmptyVoltage);
  if (fraction < 0.0f) fraction = 0.0f;
  if (fraction > 1.0f) fraction = 1.0f;
  return static_cast<int>(fraction * 100.0f + 0.5f);
}

void addEvent(const String& type, const String& message) {
  eventLog.push_back({millis(), type, message});
  if (eventLog.size() > 20) eventLog.erase(eventLog.begin());
}

void applySettings() {
  core.setReminders(settings.reminders());
  core.setSosCountdownSeconds(settings.sosCountdownSeconds());
  core.setMissedReminderMinutes(settings.missedReminderMinutes());
  core.setCallName(settings.callName().c_str());
  core.setQuietHours(settings.quietStart(),settings.quietEnd());
  core.setReminderRepeats(settings.repeatCount());
  display.configure(settings.portrait(),settings.eyeColor());
  audio.configure(settings.voiceId(), settings.voiceSpeed(), settings.voicePitch(),
                  settings.voiceVolume());
  voice.configure(settings.voiceServerUrl(),settings.voiceId(),settings.language(),settings.voiceSpeed(),settings.voiceVolume());
  if(hub.configured())voice.configureHub(hub.url(),hub.paired()?hub.token():String());
  const String timezone = settings.timezonePosix();
  setenv("TZ", timezone.c_str(), 1);
  tzset();
}

void postWebhook(const buddy::Event& event) {
  const String url = settings.webhookUrl();
  if (url.length() < 8 || WiFi.status() != WL_CONNECTED || !alertJobs) {addEvent("webhook_skipped","No connected alert endpoint; contact a caregiver directly");return;}

  DynamicJsonDocument payload(4096);
  payload["source"] = "buddy";
  payload["event"] = buddy::BuddyCore::eventName(event.type);
  payload["person"] = settings.personName();
  payload["message"] = event.message.c_str();
  payload["deviceIp"] = WiFi.localIP().toString();
  payload["uptimeMs"] = millis();
  JsonArray contacts = payload.createNestedArray("contacts");
  settings.addAlertContacts(contacts, event.type == buddy::EventType::ReminderMissed);
  String body;
  serializeJson(payload, body);

  auto* job=new AlertJob{url,body};
  const BaseType_t queued=event.type==buddy::EventType::SosEscalated?xQueueSendToFront(alertJobs,&job,0):xQueueSend(alertJobs,&job,0);
  if(queued!=pdTRUE){delete job;addEvent("webhook_failed","Alert queue full; contact a caregiver directly");}
}

void onCoreEvent(const buddy::Event& event, void*) {
  hub.event(event);
  if(event.type==buddy::EventType::CallControl&&(event.id=="end"||event.id=="decline"))voice.stopCall();
  if(event.type==buddy::EventType::SosStarted&&voice.inCall()){voice.stopCall();core.callControl("end");}
  addEvent(buddy::BuddyCore::eventName(event.type), event.message.c_str());
  if(event.type!=buddy::EventType::WakeDetected) {
    const auto date=currentLocalTime();char day[12];snprintf(day,sizeof(day),"%04u-%02u-%02u",date.year,date.month,date.day);
    char stamp[32];snprintf(stamp,sizeof(stamp),"%sT%02u:%02u:%02u",day,date.hour,date.minute,date.second);
    DynamicJsonDocument item(1024);item["type"]=buddy::BuddyCore::eventName(event.type);item["reminderId"]=event.id;item["message"]=event.message;item["date"]=day;item["at"]=stamp;item["dateKey"]=buddy::BuddyCore::dateKey(date);item["atMs"]=millis();
    File file=LittleFS.open("/care-journal.jsonl","a");if(file){serializeJson(item,file);file.print("\n");file.close();}
  }
  switch (event.type) {
    case buddy::EventType::ReminderDue:
      if(core.snapshot().speechAllowed&&!voice.audioReady())audio.chimeReminder();
      break;
    case buddy::EventType::SosStarted:
      if(!voice.audioReady())audio.chimeSos();
      break;
    case buddy::EventType::SosEscalated:
    case buddy::EventType::ReminderMissed:
      postWebhook(event);
      break;
    default:
      break;
  }
}

buddy::LocalTime currentLocalTime() {
  struct tm value {};
  buddy::LocalTime result;
  result.year=0;
  if (getLocalTime(&value, 5)) {
    result.year = value.tm_year + 1900;
    result.month = value.tm_mon + 1;
    result.day = value.tm_mday;
    result.weekday = value.tm_wday;
    result.hour = value.tm_hour;
    result.minute = value.tm_min;
    result.second = value.tm_sec;
  }
  return result;
}

void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Buddy-Pin");
  server.send(code, "application/json", body);
}

void sendStatus() {
  DynamicJsonDocument doc(6144);
  const buddy::Snapshot& state = core.snapshot();
  doc["mode"] = buddy::BuddyCore::modeName(state.mode);
  doc["headline"] = state.headline;
  doc["detail"] = state.detail;
  doc["activeReminderId"] = state.activeReminderId;
  doc["gazeX"] = state.gazeX;
  doc["gazeY"] = state.gazeY;
  doc["eyeOpen"] = state.eyeOpen;
  doc["speechSequence"] = state.speechSequence;
  doc["speechAllowed"] = state.speechAllowed;
  doc["sosSecondsRemaining"] = state.sosSecondsRemaining;
  doc["online"] = state.online;
  doc["deviceName"] = "Buddy ESP32";
  doc["ip"] = WiFi.localIP().toString();
  doc["uptimeMs"] = millis();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["freePsram"] = ESP.getFreePsram();
  doc["audioReady"] = voice.audioReady();
  doc["cameraReady"] = voice.cameraReady();
  doc["sosButtonInstalled"] = buddy_hw::kSosButtonPin >= 0;
  doc["adjustableBacklight"] = buddy_hw::kTftBacklightPin >= 0;
  doc["batteryPercent"] = currentBatteryPercent;
  doc["firmwareVersion"] = "0.2.0-wired";
  doc["audioMode"] = voice.enabled() ? "companion-gateway" : BUDDY_ENABLE_SAM ? "offline-sam" : "cue-only";

  JsonArray events = doc.createNestedArray("events");
  for (const LogEntry& item : eventLog) {
    JsonObject event = events.createNestedObject();
    event["atMs"] = item.atMs;
    event["type"] = item.type;
    event["message"] = item.message;
  }
  String output;
  serializeJson(doc, output);
  sendJson(200, output);
}

void handleAction() {
  DynamicJsonDocument body(2048);
  const DeserializationError error = deserializeJson(body, server.arg("plain"));
  if (error) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }
  const String action = body["action"] | "";
  if (action == "acknowledge") core.acknowledge(millis());
  else if (action == "snooze") core.snooze(millis(), body["minutes"] | 10);
  else if (action == "sos") core.startSos(millis());
  else if (action == "cancel_sos") core.cancelSos(millis());
  else if (action == "listen_start") core.startListening(millis());
  else if (action == "listen_stop") core.stopListening(millis());
  else if (action == "check_in") core.setCheckIn(body["mood"] | "okay", millis());
  else if (action == "look") core.lookAt(body["x"] | 0.0f, body["y"] | 0.0f, millis());
  else if (action == "say") {
    const String headline = body["headline"] | "Message from your family";
    const String detail = body["message"] | "";
    core.say(headline.c_str(), detail.c_str(), millis(), 6000);
  } else if (action == "demo_reminder") {
    buddy::Reminder reminder;
    reminder.id = "demo";
    reminder.kind = "medicine";
    reminder.title = body["title"] | "Medicine reminder";
    reminder.detail = body["detail"] | "It's time for your medicine";
    core.triggerReminder(reminder, millis());
  } else {
    sendJson(400, "{\"error\":\"Unknown action\"}");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

void handleSetTime() {
  DynamicJsonDocument body(512);
  if (deserializeJson(body, server.arg("plain"))) {
    sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }
  const int64_t epochMs = body["epochMs"] | 0LL;
  if (epochMs < 1700000000000LL) {
    sendJson(400, "{\"error\":\"Invalid clock value\"}");
    return;
  }
  struct timeval now;
  now.tv_sec = epochMs / 1000LL;
  now.tv_usec = (epochMs % 1000LL) * 1000LL;
  settimeofday(&now, nullptr);
  sendJson(200, "{\"ok\":true}");
}

void handleHubCommand(JsonObjectConst command){
 const String type=command["type"]|"";JsonObjectConst p=command["payload"];
 if(type=="say")core.say(String(p["headline"]|"Your care circle").c_str(),String(p["message"]|"").c_str(),millis(),10000);
 else if(type=="call_ring") {const String message=String(p["name"]|"Your supervisor")+" is calling. Say hey "+settings.callName()+" answer call or decline call. A camera check-in shares video for two minutes without sound.";core.say("Incoming call",message.c_str(),millis(),15000);}
 else if(type=="call_start") {const int64_t remaining=int64_t(p["expiresAt"]|int64_t(0))-int64_t(time(nullptr))*1000;if(remaining>0&&currentLocalTime().year>=2024)voice.startCall(p["callId"]|"",p["kind"]|"audio",uint32_t(std::min<int64_t>(remaining,1800000)));}
 else if(type=="call_end")voice.stopCall();
 else if(type=="action") {const String action=p["action"]|"";if(action=="sos")core.startSos(millis());else if(action=="cancel_sos")core.cancelSos(millis());else if(action=="acknowledge")core.acknowledge(millis());else if(action=="snooze")core.snooze(millis(),p["minutes"]|10);else if(action=="check_in")core.setCheckIn(p["mood"]|"okay",millis());}
}
void configureServer() {
#if !defined(BUDDY_ALLOW_LEGACY_LOCAL_API) || !BUDDY_ALLOW_LEGACY_LOCAL_API
  // Pairing is a physical-possession operation. No unauthenticated local API
  // can bypass the Care Hub's user/supervisor permissions.
  server.on("/",HTTP_GET,[]{
    if(!server.authenticate("buddy",hub.setupPin().c_str()))return server.requestAuthentication();
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"text/html",String("<!doctype html><meta name='viewport' content='width=device-width'><title>Buddy pairing</title><h1>Connect your Buddy</h1><p>Pairing code: <strong>")+(hub.paired()?"Already paired":hub.pairCode().length()?hub.pairCode():"Waiting for Care Hub connection")+"</strong></p><p>In the user's app, open My Buddy and enter this code. Codes expire in ten minutes.</p><form method='post' action='/hub'><label>Buddy app URL <input name='url' placeholder='https://your-app.onrender.com' required></label><button>Set server</button></form><p>Public servers require HTTPS and a trusted CA configured in the firmware. Plain HTTP is accepted only for a private LAN. Changing the server requires pairing again.</p>");
  });
  server.on("/api/status",HTTP_GET,[]{
    if(!server.authenticate("buddy",hub.setupPin().c_str()))return server.requestAuthentication();
    sendStatus();
  });
  server.on("/hub",HTTP_POST,[]{if(!server.authenticate("buddy",hub.setupPin().c_str()))return server.requestAuthentication();if(!hub.setServer(server.arg("url"))){server.send(400,"text/plain","Use an HTTPS server with a configured CA, or a private-LAN HTTP address.");return;}voice.stopCall();voice.configureHub(hub.url(),hub.token());server.sendHeader("Location","/");server.send(303);});
  server.onNotFound([]{sendJson(401,"{\"error\":\"Use the authenticated Care Hub app. Local settings access is disabled.\"}");});server.begin();return;
#endif
  server.on("/api/journal",HTTP_GET,[] {
    server.sendHeader("Access-Control-Allow-Origin","*");server.sendHeader("Cache-Control","no-store");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);server.send(200,"application/json","");server.sendContent("{\"events\":[");
    File file=LittleFS.open("/care-journal.jsonl","r");bool first=true;
    while(file&&file.available()){String line=file.readStringUntil('\n');DynamicJsonDocument item(1024);if(deserializeJson(item,line))continue;if(!first)server.sendContent(",");first=false;server.sendContent(line);}
    if(file)file.close();server.sendContent("]}");server.sendContent("");
  });
  server.on("/api/voice",HTTP_POST,[] {
    DynamicJsonDocument body(2048);if(deserializeJson(body,server.arg("plain"))){sendJson(400,"{\"error\":\"Invalid JSON\"}");return;}
    const auto outcome=core.hear(String(body["text"]|"").c_str(),millis(),currentLocalTime());
    if(outcome=="assistant") {
      if(voice.enabled())voice.ask(core.assistantQuery().c_str());
      else core.say("Voice connection","General conversation needs the companion voice server.",millis());
    }
    DynamicJsonDocument reply(2048);reply["outcome"]=outcome;reply["query"]=core.assistantQuery();reply["answer"]=core.snapshot().detail;
    String output;serializeJson(reply,output);sendJson(200,output);
  });
  server.on("/api/display/frame",HTTP_GET,[] {
    const auto& f=display.frame();uint8_t header[16]={'B','D','Y','1'};uint32_t bytes=f.width()*f.height()*2,seq=millis();
    header[4]=f.width()&255;header[5]=f.width()>>8;header[6]=f.height()&255;header[7]=f.height()>>8;
    for(int i=0;i<4;i++){header[8+i]=(seq>>(8*i))&255;header[12+i]=(bytes>>(8*i))&255;}
    server.sendHeader("Access-Control-Allow-Origin","*");server.sendHeader("Cache-Control","no-store");server.setContentLength(bytes+16);server.send(200,"application/octet-stream","");
    server.sendContent(reinterpret_cast<const char*>(header),16);server.sendContent(reinterpret_cast<const char*>(f.pixels().data()),bytes);
  });
  server.on("/api/calendar",HTTP_GET,[] {
    String month=server.arg("month");if(month.length()!=7){sendJson(400,"{\"error\":\"Use YYYY-MM\"}");return;}
    int year=month.substring(0,4).toInt(),m=month.substring(5).toInt();
    if(year<2024||year>2100||m<1||m>12){sendJson(400,"{\"error\":\"Invalid month\"}");return;}
    DynamicJsonDocument config(49152);if(deserializeJson(config,settings.toJson())){sendJson(507,"{\"error\":\"Insufficient calendar memory\"}");return;}
    std::map<std::string,std::string> history;
    File file=LittleFS.open("/care-journal.jsonl","r");
    while(file&&file.available()){DynamicJsonDocument item(1024);if(deserializeJson(item,file.readStringUntil('\n')))continue;String date=item["date"]|"",type=item["type"]|"";if(date.startsWith(month)&&type.startsWith("reminder_")){String key=date+"|"+String(item["reminderId"]|"");history[key.c_str()]=type.substring(9).c_str();}}
    if(file)file.close();
    const auto now=currentLocalTime();char today[12];snprintf(today,sizeof(today),"%04u-%02u-%02u",now.year,now.month,now.day);
    server.sendHeader("Access-Control-Allow-Origin","*");server.sendHeader("Cache-Control","no-store");server.setContentLength(CONTENT_LENGTH_UNKNOWN);server.send(200,"application/json","");
    server.sendContent(String("{\"month\":\"")+month+"\",\"source\":\"BuddyCore C++\",\"today\":\""+today+"\",\"days\":[");
    for(int d=1;d<=31;d++){
      struct tm t={};t.tm_year=year-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_hour=12;mktime(&t);if(t.tm_mon!=m-1)break;
      buddy::LocalTime date;date.year=year;date.month=m;date.day=d;date.weekday=t.tm_wday;
      char label[12];snprintf(label,sizeof(label),"%04d-%02d-%02d",year,m,d);if(d>1)server.sendContent(",");server.sendContent(String("{\"date\":\"")+label+"\",\"items\":[");bool first=true;
      auto reminders=core.reminders();std::sort(reminders.begin(),reminders.end(),[](const buddy::Reminder& a,const buddy::Reminder& b){return a.hour*60+a.minute<b.hour*60+b.minute;});
      for(const auto& r:reminders)if(buddy::BuddyCore::occursOn(r,date))for(JsonObject item:config["reminders"].as<JsonArray>())if(String(item["id"]|"")==r.id.c_str()){
        DynamicJsonDocument entry(2048);entry.set(item);auto status=history.find(std::string(label)+"|"+r.id);entry["status"]=status==history.end()?"scheduled":status->second.c_str();String output;serializeJson(entry,output);if(!first)server.sendContent(",");first=false;server.sendContent(output);break;
      }
      server.sendContent("]}");
    }
    server.sendContent("]}");server.sendContent("");
  });
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/settings", HTTP_GET, [] { sendJson(200, settings.toJson()); });
  server.on("/api/settings", HTTP_PUT, [] {
    String error;
    if (!settings.replaceFromJson(server.arg("plain"), error)) {
      DynamicJsonDocument response(512);
      response["error"] = error;
      String body;
      serializeJson(response, body);
      sendJson(400, body);
      return;
    }
    applySettings();
    addEvent("settings", "Care settings updated");
    sendJson(200, settings.toJson());
  });
  server.on("/api/actions", HTTP_POST, handleAction);
  server.on("/api/time", HTTP_POST, handleSetTime);
  server.on("/api/health", HTTP_GET, [] {
    sendJson(200, "{\"ok\":true,\"name\":\"Buddy\",\"apiVersion\":1}");
  });
  server.on("/api/reset-wifi", HTTP_POST, [] {
    sendJson(200, "{\"ok\":true,\"message\":\"Restarting in setup mode\"}");
    delay(250);
    WiFiManager manager;
    manager.resetSettings();
    ESP.restart();
  });
  server.onNotFound([] {
    if (server.method() == HTTP_OPTIONS) {
      sendJson(204, "");
      return;
    }
    String path = server.uri();
    if (path.endsWith("/")) path += "index.html";
    if (LittleFS.exists(path)) {
      File file = LittleFS.open(path, "r");
      String contentType = "text/plain";
      if (path.endsWith(".html")) contentType = "text/html";
      else if (path.endsWith(".css")) contentType = "text/css";
      else if (path.endsWith(".js")) contentType = "application/javascript";
      else if (path.endsWith(".json")) contentType = "application/json";
      else if (path.endsWith(".svg")) contentType = "image/svg+xml";
      server.streamFile(file, contentType);
      file.close();
      return;
    }
    sendJson(404, "{\"error\":\"Not found\"}");
  });
  server.begin();
}

void updateButtons() {
  if (buddy_hw::kSosButtonPin < 0) return;
  const bool sosDown = digitalRead(buddy_hw::kSosButtonPin) ==
                       (buddy_hw::kButtonsActiveLow ? LOW : HIGH);

  if (sosDown && !sosWasDown) {
    sosDownSince = millis();
    sosLongPressHandled = false;
  }
  if (sosDown && !sosLongPressHandled && millis() - sosDownSince >= 1200) {
    sosLongPressHandled = true;
    if(core.snapshot().mode==buddy::Mode::SosCountdown || core.snapshot().mode==buddy::Mode::SosActive)core.cancelSos(millis());
    else core.startSos(millis());
  }
  if(!sosDown&&sosWasDown&&!sosLongPressHandled&&millis()-sosDownSince>40&&voice.inCall())core.callControl("end");
  sosWasDown = sosDown;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nBuddy 0.2.0-wired | %s | flash %u MB | PSRAM %u MB\n",
                ESP.getChipModel(),unsigned(ESP.getFlashChipSize()/1048576),
                unsigned(ESP.getPsramSize()/1048576));
  Serial.printf("TFT MOSI=%d SCK=%d CS=%d DC=%d RESET=%d MISO=%d; SOS=%d\n",
                TFT_MOSI,TFT_SCLK,TFT_CS,TFT_DC,TFT_RST,TFT_MISO,buddy_hw::kSosButtonPin);
  if (buddy_hw::kSosButtonPin >= 0) pinMode(buddy_hw::kSosButtonPin, INPUT_PULLUP);
  if (buddy_hw::kBatterySensePin >= 0) pinMode(buddy_hw::kBatterySensePin, INPUT);

  display.begin();
  audio.begin();
  voice.begin();
  settings.begin();
  hub.begin();
  alertJobs=xQueueCreate(4,sizeof(AlertJob*));alertResults=xQueueCreate(4,sizeof(int));
  if(!alertJobs||!alertResults||xTaskCreatePinnedToCore(alertWorker,"buddy-alerts",8192,nullptr,1,nullptr,0)!=pdPASS)alertJobs=nullptr;
  applySettings();
  core.setEventCallback(onCoreEvent, nullptr);
  core.begin(millis());

  display.showBoot("BUDDY", "Connecting to Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFiManager wifiManager;
  WiFiManagerParameter hubAddress("hub","Buddy app URL",hub.url().c_str(),180);wifiManager.addParameter(&hubAddress);
  wifiManager.setConfigPortalTimeout(180);
  const bool connected = wifiManager.autoConnect("Buddy-Setup", "carebuddy");
  if (!hub.setServer(hubAddress.getValue()))
    Serial.println("Buddy server URL rejected: public addresses need HTTPS and a configured CA; HTTP is private-LAN only.");
  core.setOnline(connected);

  if (connected) {
    WiFi.setSleep(false);
    configTzTime(settings.timezonePosix().c_str(), "pool.ntp.org", "time.google.com");
    if (MDNS.begin("buddy")) MDNS.addService("http", "tcp", 80);
    addEvent("boot", "Buddy connected at " + WiFi.localIP().toString());
    Serial.println("Buddy setup/status: http://"+WiFi.localIP().toString());
  } else {
    addEvent("boot", "Buddy started offline");
  }
  configureServer();
  if(voice.audioReady())voice.chime();
  audio.speak("Buddy is ready.");
}

void loop() {
  int alertResult=0;
  while(alertResults&&xQueueReceive(alertResults,&alertResult,0)==pdTRUE)addEvent("webhook",alertResult>=200&&alertResult<300?"Alert endpoint accepted the request; caregiver receipt is unconfirmed":"Alert delivery failed; contact a caregiver directly");
  server.handleClient();
  if(hub.poll(settings,core.snapshot(),currentBatteryPercent,voice.cameraReady(),voice.audioReady(),handleHubCommand))applySettings();
  if(hub.configured())voice.configureHub(hub.url(),hub.paired()?hub.token():String());
  core.setCameraActive(voice.cameraActive());
  if(!voice.inCall())display.setVideoActive(false);
  String callFrame;if(voice.receiveFrame(callFrame)&&voice.inCall())display.setVideoFrame(callFrame);
  updateButtons();
  if (static_cast<int32_t>(millis() - nextBatterySampleMs) >= 0) {
    currentBatteryPercent = readBatteryPercent();
    nextBatterySampleMs = millis() + 5000;
  }
  core.setOnline(WiFi.status() == WL_CONNECTED);
  const buddy::LocalTime localTime = currentLocalTime();
  String heard;uint8_t kind=0;
  while(voice.receive(heard,kind)) {
    if(kind==2){addEvent("voice_error",heard);audio.chimeReminder();}
    else if(kind==1)core.say("Buddy says",heard.c_str(),millis(),15000);
    else {
      const auto outcome=core.hear(heard.c_str(),millis(),localTime);
      if(outcome=="assistant")voice.ask(core.assistantQuery().c_str());
    }
  }
  if(localTime.year>=2024 && restoredDate!=buddy::BuddyCore::dateKey(localTime)) {
    restoredDate=buddy::BuddyCore::dateKey(localTime);
    File file=LittleFS.open("/care-journal.jsonl","r");
    while(file&&file.available()){DynamicJsonDocument item(1024);if(!deserializeJson(item,file.readStringUntil('\n')) && uint32_t(item["dateKey"]|0)==restoredDate && String(item["type"]|"")=="reminder_acknowledged")core.restoreOccurrence(String(item["reminderId"]|"").c_str(),restoredDate);}
    if(file)file.close();
  }
  if(millis()-lastCoreTick>=50){lastCoreTick=millis();core.tick(lastCoreTick,localTime);}
  const auto& snapshot=core.snapshot();
  if(snapshot.speechSequence!=spokenSequence) {
    spokenSequence=snapshot.speechSequence;
    if(snapshot.speechAllowed) {
      const String phrase=String(snapshot.headline.c_str())+". "+snapshot.detail.c_str();
      if(voice.enabled()) {
        if(snapshot.mode==buddy::Mode::Reminder)voice.chime();
        voice.speak(phrase,snapshot.mode==buddy::Mode::SosCountdown||snapshot.mode==buddy::Mode::SosActive);
      }
      else {
        audio.speak(phrase);
        if(voice.audioReady()&&(snapshot.mode==buddy::Mode::Reminder||snapshot.mode==buddy::Mode::SosCountdown||snapshot.mode==buddy::Mode::SosActive))
          voice.chime(snapshot.mode!=buddy::Mode::Reminder);
      }
    }
  }
  display.render(core.snapshot(), localTime, currentBatteryPercent,
                 settings.brightness());
  delay(3);
}
