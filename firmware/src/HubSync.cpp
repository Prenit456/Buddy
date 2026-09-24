#include "HubSync.h"
#include "HubHttp.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_system.h>

void HubSync::begin(){
 prefs_.begin("buddy-hub",false);url_=prefs_.getString("url",BUDDY_HUB_URL);token_=prefs_.getString("token","");revision_=prefs_.getUInt("revision",0);paired_=prefs_.getBool("paired",false);
 char pin[9];snprintf(pin,sizeof(pin),"%08lu",(unsigned long)(esp_random()%100000000));setupPin_=pin;
 Serial.println("Buddy local setup PIN: "+setupPin_);
 File f=LittleFS.open("/hub-outbox.json","r");if(!f||deserializeJson(outbox_,f)){outbox_.clear();outbox_.createNestedArray("events");outbox_.createNestedArray("ack");}if(f)f.close();
 jobs_=xQueueCreate(1,sizeof(Job*));results_=xQueueCreate(1,sizeof(Job*));
 if(!jobs_||!results_||xTaskCreatePinnedToCore(worker,"buddy-sync",10240,this,1,nullptr,0)!=pdPASS){jobs_=nullptr;syncError_="Could not start care-circle sync";}
}
bool HubSync::setServer(String url){url.trim();while(url.endsWith("/"))url.remove(url.length()-1);if(!allowedHubUrl(url)||url.startsWith("https://")&&strlen(BUDDY_HUB_CA)==0)return false;if(url==url_)return true;url_=url;token_="";revision_=0;paired_=false;pairCode_="";prefs_.putString("url",url_);prefs_.remove("token");prefs_.putUInt("revision",0);prefs_.putBool("paired",false);return true;}
void HubSync::persistOutbox(){File f=LittleFS.open("/hub-outbox.tmp","w");if(!f){syncError_="Event storage is full";return;}const size_t written=serializeJson(outbox_,f);f.close();if(!written||!LittleFS.rename("/hub-outbox.tmp","/hub-outbox.json"))syncError_="Could not save care events";}
void HubSync::event(const buddy::Event& e){
 if(e.type==buddy::EventType::WakeDetected)return;
 JsonArray events=outbox_["events"].as<JsonArray>();if(events.size()>=40){syncError_="Offline event queue full; some old events were dropped";events.remove(0);}
 JsonObject item=events.createNestedObject();char id[26];snprintf(id,sizeof(id),"%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());item["eventId"]=id;item["type"]=buddy::BuddyCore::eventName(e.type);item["reminderId"]=e.id;item["message"]=e.message;item["createdAt"]=int64_t(time(nullptr))*1000;persistOutbox();
}
void HubSync::worker(void* context){auto* self=static_cast<HubSync*>(context);for(;;){Job* job=nullptr;if(xQueueReceive(self->jobs_,&job,portMAX_DELAY)!=pdTRUE)continue;HTTPClient http;WiFiClient plain;WiFiClientSecure secure;if(beginHubHttp(http,plain,secure,job->url+job->path,job->token)){job->code=http.POST(job->body);if(job->code>0&&http.getSize()<196608)job->response=http.getString();http.end();}xQueueSend(self->results_,&job,portMAX_DELAY);}}
bool HubSync::poll(SettingsStore& settings,const buddy::Snapshot& status,int battery,bool camera,bool audio,CommandHandler handler){
 bool changed=false;Job* result=nullptr;
 if(results_&&xQueueReceive(results_,&result,0)==pdTRUE){waiting_=false;next_=millis()+3000;
  DynamicJsonDocument data(98304);
  if(result->url!=url_){delete result;return false;}
  if(result->code>=200&&result->code<300&&!deserializeJson(data,result->response)){
   if(data["deviceToken"].is<const char*>()){token_=data["deviceToken"].as<String>();prefs_.putString("token",token_);}
   if(data["pairCode"].is<const char*>()){pairCode_=data["pairCode"].as<String>();codeExpires_=millis()+540000;Serial.println("Buddy app pairing code: "+pairCode_);}
   if(data.containsKey("paired")){
    const bool paired=data["paired"]|false;if(paired!=paired_){paired_=paired;prefs_.putBool("paired",paired_);}if(paired_)pairCode_="";
    if(data["settings"].is<JsonObject>()){
     String incoming,error;serializeJson(data["settings"],incoming);
     if(settings.replaceFromJson(incoming,error)){revision_=data["revision"]|0;prefs_.putUInt("revision",revision_);syncError_="";changed=true;}else syncError_=error;
    }
    // Remove only items in the successful upload; new events stay queued.
    DynamicJsonDocument sent(40960);if(paired_)deserializeJson(sent,result->body);
    for(const char* key:{"events","ack"}){JsonArray local=outbox_[key];JsonArray uploaded=sent[key];for(int i=int(local.size())-1;i>=0;i--)for(JsonVariant item:uploaded){const bool same=String(key)=="events"?local[i]["eventId"].as<String>()==item["eventId"].as<String>():local[i].as<String>()==item.as<String>();if(same){local.remove(i);break;}}}
    for(JsonObjectConst command:data["commands"].as<JsonArrayConst>()){
     const String commandId=command["id"]|"";bool duplicate=false;for(JsonVariant item:outbox_["ack"].as<JsonArray>())if(item.as<String>()==commandId)duplicate=true;
     if(!duplicate&&handler){handler(command);outbox_["ack"].add(commandId);}
    }
    persistOutbox();
   }
  }else{syncError_="Care Hub unavailable or TLS certificate not configured";next_=millis()+10000;}
  delete result;
 }
 if(!jobs_||waiting_||!configured()||WiFi.status()!=WL_CONNECTED||int32_t(millis()-next_)<0)return changed;
 auto* job=new Job();job->url=url_;job->token=token_;
 DynamicJsonDocument body(40960);
 if(token_.isEmpty()){job->path="/api/v2/device/enroll";body["name"]="Buddy ESP32";body["capabilities"]["camera"]=camera;body["capabilities"]["audio"]=audio;}
 else if(!paired_&&(pairCode_.isEmpty()||int32_t(millis()-codeExpires_)>=0)){job->path="/api/v2/device/pair-code";}
 else {job->path="/api/v2/device/sync";body["appliedRevision"]=revision_;body["syncError"]=syncError_;body["capabilities"]["camera"]=camera;body["capabilities"]["audio"]=audio;body["events"].set(outbox_["events"]);body["ack"].set(outbox_["ack"]);JsonObject s=body.createNestedObject("status");s["mode"]=buddy::BuddyCore::modeName(status.mode);s["headline"]=status.headline;s["detail"]=status.detail;s["batteryPercent"]=battery;s["activeReminderId"]=status.activeReminderId;}
 serializeJson(body,job->body);if(xQueueSend(jobs_,&job,0)==pdTRUE)waiting_=true;else delete job;return changed;
}
