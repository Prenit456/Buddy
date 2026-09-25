#pragma once
#include <stdint.h>

#ifndef BUDDY_ENABLE_SAM
#define BUDDY_ENABLE_SAM 0
#endif

// September 23 assembly: shared I2S clocks, OV7670, SPI TFT without touch.
// TFT signal definitions must also match platformio.ini.
#ifndef BUDDY_SOS_PIN
#define BUDDY_SOS_PIN -1
#endif
#ifndef BUDDY_MIC_SHIFT
#define BUDDY_MIC_SHIFT 14
#endif
#ifndef BUDDY_VAD_THRESHOLD
#define BUDDY_VAD_THRESHOLD 600
#endif

namespace buddy_hw {

constexpr int kTftBacklightPin = -1;  // LED is wired directly to 3V3.
constexpr int kSosButtonPin = BUDDY_SOS_PIN;  // No button in the supplied wiring.
constexpr int kBuzzerPin = -1;  // Cues use the MAX98357A speaker.

// Set this to the ADC pin connected through a Li-ion voltage divider. It is
// disabled until the exact battery/charging board is known. While disabled,
// Buddy displays the fallback percentage below.
constexpr int kBatterySensePin = -1;
constexpr float kBatteryDividerRatio = 2.0f;
constexpr float kBatteryEmptyVoltage = 3.20f;
constexpr float kBatteryFullVoltage = 4.20f;
constexpr int kBatteryFallbackPercent = -1; // unknown until a battery sensor is fitted

// MAX98357A and microphone share ONE full-duplex I2S controller.
constexpr int kI2sBclkPin = 18;
constexpr int kI2sLrcPin = 21;
constexpr int kI2sDataOutPin = 2;

// Standard I2S microphone; L/R is tied to GND (left slot).
constexpr int kMicBclkPin = kI2sBclkPin;
constexpr int kMicLrcPin = kI2sLrcPin;
constexpr int kMicDataPin = 1;
constexpr unsigned kMicShift = BUDDY_MIC_SHIFT;
constexpr unsigned kVadThreshold = BUDDY_VAD_THRESHOLD;
static_assert(kMicShift >= 8 && kMicShift <= 24, "Invalid microphone sample shift");

constexpr bool kButtonsActiveLow = true;
constexpr uint8_t kScreenRotation = 1;  // 320x240 landscape
constexpr uint8_t kBacklightChannel = 7;

}  // namespace buddy_hw
