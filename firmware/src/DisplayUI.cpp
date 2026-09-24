#include "DisplayUI.h"
#include "HardwareConfig.h"
#include "VoiceGateway.h"
#if BUDDY_ENABLE_NETWORK_VOICE
#include <TJpg_Decoder.h>
#include <mbedtls/base64.h>
#endif
DisplayUI* DisplayUI::decoding_=nullptr;
bool DisplayUI::decodeBlock(int16_t x,int16_t y,uint16_t w,uint16_t h,uint16_t* data){
 if(!decoding_)return false;
 for(uint16_t row=0;row<h;row++)for(uint16_t col=0;col<w;col++)if(x+col>=0&&x+col<320&&y+row>=0&&y+row<240)decoding_->videoPixels_[(y+row)*320+x+col]=data[row*w+col];return true;
}
void DisplayUI::setVideoFrame(const String& jpeg){
#if BUDDY_ENABLE_NETWORK_VOICE
 if(jpeg.length()>240000)return;auto* bytes=static_cast<uint8_t*>(malloc(jpeg.length()*3/4+4));if(!bytes)return;size_t size=0;
 if(mbedtls_base64_decode(bytes,jpeg.length()*3/4+4,&size,reinterpret_cast<const unsigned char*>(jpeg.c_str()),jpeg.length())==0){uint16_t w=0,h=0;if(TJpgDec.getJpgSize(&w,&h,bytes,size)==JDR_OK&&w==320&&h==240){videoPixels_.resize(320*240);decoding_=this;TJpgDec.setJpgScale(1);TJpgDec.setSwapBytes(false);TJpgDec.setCallback(decodeBlock);if(TJpgDec.drawJpg(0,0,bytes,size)==JDR_OK)videoActive_=true;decoding_=nullptr;}}
 free(bytes);
#endif
}
void DisplayUI::begin() {
  if (buddy_hw::kTftBacklightPin >= 0) {
    pinMode(buddy_hw::kTftBacklightPin, OUTPUT);
    ledcSetup(buddy_hw::kBacklightChannel, 5000, 8);
    ledcAttachPin(buddy_hw::kTftBacklightPin, buddy_hw::kBacklightChannel);
  }
  tft_.init(); tft_.setSwapBytes(true);
  configure(false, 0x4E9F);
}
void DisplayUI::configure(bool portrait, uint16_t color) {
  tft_.setRotation(portrait ? 0 : 1);
  renderer_.configure(portrait, color);
}
void DisplayUI::showBoot(const String&, const String&) {
  buddy::Snapshot state; buddy::LocalTime clock;
  clock.year=0;
  render(state, clock, -1, 180);
}
void DisplayUI::render(const buddy::Snapshot& state, const buddy::LocalTime& time, int battery, uint8_t brightness) {
  if (millis()-lastFrameMs_<50) return;
  lastFrameMs_=millis();
  if (buddy_hw::kTftBacklightPin >= 0 && brightness!=lastBrightness_) { ledcWrite(buddy_hw::kBacklightChannel,brightness); lastBrightness_=brightness; }
  renderer_.render(state,time,battery,millis());
  if(videoActive_&&videoPixels_.size()==320*240&&state.mode!=buddy::Mode::SosActive&&state.mode!=buddy::Mode::SosCountdown){
    // Fit incoming landscape video under the clock in either TFT orientation.
    const int width=std::min(renderer_.width(),(renderer_.height()-40)*4/3),height=width*3/4,top=34+(renderer_.height()-34-height)/2,left=(renderer_.width()-width)/2;
    videoComposite_=renderer_.pixels();std::fill(videoComposite_.begin()+34*renderer_.width(),videoComposite_.end(),0x0863);
    for(int y=0;y<height;y++)for(int x=0;x<width;x++)videoComposite_[(top+y)*renderer_.width()+left+x]=videoPixels_[(y*240/height)*320+x*320/width];
    for(int y=6;y<13;y++)for(int x=renderer_.width()-9;x<renderer_.width()-3;x++)videoComposite_[y*renderer_.width()+x]=TFT_RED;
    tft_.pushImage(0,0,renderer_.width(),renderer_.height(),videoComposite_.data());return;
  }
  tft_.pushImage(0,0,renderer_.width(),renderer_.height(),const_cast<uint16_t*>(renderer_.pixels().data()));
}
