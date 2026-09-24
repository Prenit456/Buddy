#pragma once
#include <BuddyCore.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace buddy {
// One RGB565 framebuffer, 153,600 bytes in either orientation.
// RGB565 renderer used by the ESP32 display adapter.
class BuddyDisplay {
 public:
  BuddyDisplay();
  ~BuddyDisplay();
  void configure(bool portrait, uint16_t eyeColor = 0x4E9F);
  void render(const Snapshot& state, const LocalTime& time, int battery, uint32_t nowMs);
  int width() const;
  int height() const;
  const std::vector<uint16_t>& pixels() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
