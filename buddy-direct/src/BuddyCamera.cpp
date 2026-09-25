#include "BuddyCamera.h"
#include "CameraConfig.h"
#include "HardwareConfig.h"
#if BUDDY_ENABLE_CAMERA
#include <esp_camera.h>
#include <img_converters.h>
#include <mbedtls/base64.h>

bool BuddyCamera::open() {
  using namespace buddy_camera;
  const int pins[] = {xclk,sda,scl,d0,d1,d2,d3,d4,d5,d6,d7,vsync,href,pclk};
  const int reserved[] = {TFT_MOSI,TFT_MISO,TFT_SCLK,TFT_CS,TFT_DC,TFT_RST,
    buddy_hw::kTftBacklightPin,buddy_hw::kSosButtonPin,buddy_hw::kBuzzerPin,
    buddy_hw::kI2sBclkPin,buddy_hw::kI2sLrcPin,buddy_hw::kI2sDataOutPin,
    buddy_hw::kMicDataPin};
  for (size_t i=0; i<sizeof(pins)/sizeof(pins[0]); ++i) {
    if (pins[i]<0) return false;
    for (int pin: reserved) if (pin==pins[i]) {
      Serial.printf("Camera GPIO %d conflicts with another component\n",pin);
      return false;
    }
    for (size_t j=0; j<i; ++j) if (pins[i]==pins[j]) return false;
  }
  if (!psramFound()) {
    Serial.println("Camera needs PSRAM. Check the module and build memory settings.");
    return false;
  }
  camera_config_t c = {};
  c.pin_pwdn=pwdn; c.pin_reset=reset; c.pin_xclk=xclk;
  c.pin_sccb_sda=sda; c.pin_sccb_scl=scl;
  c.pin_d0=d0; c.pin_d1=d1; c.pin_d2=d2; c.pin_d3=d3;
  c.pin_d4=d4; c.pin_d5=d5; c.pin_d6=d6; c.pin_d7=d7;
  c.pin_vsync=vsync; c.pin_href=href; c.pin_pclk=pclk;
  c.xclk_freq_hz=20000000;
  c.ledc_timer=LEDC_TIMER_0; c.ledc_channel=LEDC_CHANNEL_0;
  // OV7670 produces RGB565, not JPEG. Convert before sending to the hub.
  c.pixel_format=PIXFORMAT_RGB565; c.frame_size=FRAMESIZE_QVGA;
  c.jpeg_quality=18; c.fb_count=1;
  c.fb_location=CAMERA_FB_IN_PSRAM; c.grab_mode=CAMERA_GRAB_WHEN_EMPTY;
  const esp_err_t error=esp_camera_init(&c);
  opened_=error==ESP_OK;
  if (!opened_) Serial.printf("Camera init failed: %s (0x%x)\n",esp_err_to_name(error),error);
  else {
    sensor_t* sensor=esp_camera_sensor_get();
    Serial.printf("Camera ready: sensor PID 0x%02x, RGB565 320x240\n",sensor? sensor->id.PID:0);
  }
  return opened_;
}

bool BuddyCamera::begin() { ready_=open(); stop(); return ready_; }

void BuddyCamera::stop() {
  if (opened_) { esp_camera_deinit(); opened_=false; }
}

String BuddyCamera::jpeg() {
  if (!ready_ || (!opened_ && !open())) return "";
  camera_fb_t* frame=esp_camera_fb_get();
  if (!frame) { Serial.println("Camera capture timed out"); return ""; }
  uint8_t* jpg=nullptr;
  size_t length=0;
  const bool encoded=frame2jpg(frame,65,&jpg,&length);
  esp_camera_fb_return(frame);
  String result;
  if (encoded && jpg && length>0 && length<60000) {
    const size_t capacity=4*((length+2)/3)+1;
    auto* buffer=static_cast<unsigned char*>(malloc(capacity));
    if (buffer) {
      size_t written=0;
      if (mbedtls_base64_encode(buffer,capacity,&written,jpg,length)==0) {
        buffer[written]=0;
        result=reinterpret_cast<char*>(buffer);
      }
      free(buffer);
    }
  } else Serial.println("Camera JPEG conversion failed or exceeded the frame limit");
  free(jpg);
  return result;
}
#else
bool BuddyCamera::begin(){return false;}
String BuddyCamera::jpeg(){return "";}
bool BuddyCamera::open(){return false;}
void BuddyCamera::stop(){}
#endif
