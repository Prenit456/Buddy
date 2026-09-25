#pragma once
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "BuddyCamera.h"
#ifndef BUDDY_ENABLE_NETWORK_VOICE
#define BUDDY_ENABLE_NETWORK_VOICE 0
#endif

// Optional INMP441-compatible I2S mic + MAX98357A speaker adapter.
// The companion server holds provider keys. A worker owns all blocking I/O;
// the main loop remains responsible for reminders, display and SOS.
class VoiceGateway {
 public:
  void begin();
  void configure(const String& url,const String& voice,const String& language,uint8_t rate,uint8_t volume);
  void speak(const String& text, bool urgent=false);
  void chime(bool sos=false);
  void ask(const String& text);
  // kind: 0 transcript, 1 assistant answer, 2 connection error (never re-speak).
  bool receive(String& text, uint8_t& kind);
  bool enabled() const;
  bool audioReady()const;
  bool cameraReady()const;
  void configureHub(const String& base,const String& token);
  void startCall(const String& id,const String& kind,uint32_t durationMs);
  void stopCall();
  bool inCall()const;
  bool cameraActive()const;
  bool receiveFrame(String& jpeg);
 private:
#if BUDDY_ENABLE_NETWORK_VOICE
  struct Job { uint8_t kind; char text[601]; };
  QueueHandle_t jobs_=nullptr,results_=nullptr;
  SemaphoreHandle_t mutex_=nullptr;
  String url_,voice_="bella",language_="en-IN";
  String hub_,token_,callId_,callKind_;
  BuddyCamera camera_;
  QueueHandle_t frames_=nullptr;
  std::atomic<uint32_t> callUntil_{0};
  uint8_t rate_=72,volume_=82;
  std::atomic<bool> ready_{false},configured_{false};
  std::atomic<uint32_t> generation_{0};
  static void task(void* self);
  void run();
  void reportError(const char* message);
  bool playPcm(const uint8_t* pcm,size_t bytes,uint8_t volume,uint32_t generation);
  void playChime(bool sos,uint8_t volume,uint32_t generation);
  void mediaStep(const String& base,const String& token,const String& callId,const String& kind,uint8_t volume);
#endif
};
