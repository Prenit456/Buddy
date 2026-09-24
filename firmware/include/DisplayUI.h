#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <BuddyCore.h>
#include <BuddyDisplay.h>

class DisplayUI {
 public:
  void begin();
  void configure(bool portrait, uint16_t color);
  const buddy::BuddyDisplay& frame() const { return renderer_; }
  void render(const buddy::Snapshot& state, const buddy::LocalTime& localTime,
              int batteryPercent, uint8_t brightness);
  void showBoot(const String& line1, const String& line2 = "");
  void setVideoFrame(const String& jpeg);
  void setVideoActive(bool active){videoActive_=active;}

 private:
  TFT_eSPI tft_;
  buddy::BuddyDisplay renderer_;
  uint32_t lastFrameMs_ = 0;
  uint8_t lastBrightness_ = 255;
  std::vector<uint16_t> videoPixels_;
  std::vector<uint16_t> videoComposite_;
  bool videoActive_=false;
  static DisplayUI* decoding_;
  static bool decodeBlock(int16_t x,int16_t y,uint16_t w,uint16_t h,uint16_t* data);
};
