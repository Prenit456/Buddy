#pragma once
#include <Arduino.h>
class BuddyCamera {
 public:
  bool begin();
  bool ready()const{return ready_;}
  String jpeg(); // Worker-owned capture, only called during an accepted call.
  void stop();
 private:
  bool open();
  bool ready_=false,opened_=false;
};
