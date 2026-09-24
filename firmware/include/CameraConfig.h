#pragma once
// OV7670 parallel camera, wired as agreed in the Hardware task.
// RET is tied to 3V3; PWDN is tied to GND. Neither uses an ESP32 GPIO.
#ifndef BUDDY_ENABLE_CAMERA
#define BUDDY_ENABLE_CAMERA 0
#endif
namespace buddy_camera {
constexpr int pwdn=-1, reset=-1, xclk=12, sda=16, scl=17;
constexpr int d0=4,d1=5,d2=6,d3=7,d4=8,d5=9,d6=10,d7=11;
constexpr int vsync=14,href=15,pclk=13;
}
