#pragma once
#include "HardwareConfig.h"
#include "CameraConfig.h"

namespace buddy_hw {
// Shared BCLK/WS are listed once. Every other connected signal must be unique.
constexpr int kUsedSignals[] = {
  TFT_MOSI,TFT_MISO,TFT_SCLK,TFT_CS,TFT_DC,TFT_RST,
  kTftBacklightPin,kSosButtonPin,kBuzzerPin,kBatterySensePin,
  kI2sBclkPin,kI2sLrcPin,kI2sDataOutPin,kMicDataPin,
#if BUDDY_ENABLE_CAMERA
  buddy_camera::xclk,buddy_camera::sda,buddy_camera::scl,
  buddy_camera::d0,buddy_camera::d1,buddy_camera::d2,buddy_camera::d3,
  buddy_camera::d4,buddy_camera::d5,buddy_camera::d6,buddy_camera::d7,
  buddy_camera::vsync,buddy_camera::href,buddy_camera::pclk,
  buddy_camera::pwdn,buddy_camera::reset,
#endif
};
constexpr bool validSignals() {
  for(unsigned i=0;i<sizeof(kUsedSignals)/sizeof(kUsedSignals[0]);++i) {
    const int pin=kUsedSignals[i];
    if(pin<0)continue;
    if(pin==0||pin==3||pin==19||pin==20||(pin>=22&&pin<=37)||
       (pin>=43&&pin<=46)||pin>=48)return false;
    for(unsigned j=0;j<i;++j)if(pin==kUsedSignals[j])return false;
  }
  return kMicBclkPin==kI2sBclkPin&&kMicLrcPin==kI2sLrcPin;
}
static_assert(validSignals(),"GPIO collision or reserved GPIO in Buddy wiring; check HardwareConfig and CameraConfig");
}  // namespace buddy_hw
