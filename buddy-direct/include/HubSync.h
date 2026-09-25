#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <ctime>
#include <BuddyCore.h>
#include "SettingsStore.h"

// All flash/settings/core mutations stay on the Arduino main loop. The worker
// only performs bounded HTTP requests and passes responses back through queues.
class HubSync {
 public:
  using CommandHandler=void(*)(JsonObjectConst);
  void begin();
  bool setServer(String url);
  const String& url()const{return url_;}
  const String& token()const{return token_;}
  const String& pairCode()const{return pairCode_;}
  const String& setupPin()const{return setupPin_;}
  bool configured()const{return url_.length()>0;}
  bool paired()const{return paired_;}
  bool poll(SettingsStore& settings,const buddy::Snapshot& status,int battery,bool camera,bool audio,CommandHandler handler);
  void event(const buddy::Event& event);
 private:
  struct Job {String url,token,path,body,response;int code=0;};
  static void worker(void* self);
  void persistOutbox();
  Preferences prefs_;
  String url_,token_,pairCode_,setupPin_,syncError_;
  uint32_t revision_=0,next_=0,codeExpires_=0;
  bool waiting_=false,paired_=false;
  QueueHandle_t jobs_=nullptr,results_=nullptr;
  DynamicJsonDocument outbox_{32768};
};
