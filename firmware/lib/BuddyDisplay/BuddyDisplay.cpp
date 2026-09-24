#include "BuddyDisplay.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

// Isolate upstream Arduino helpers and macros from the application.
namespace buddy_robo {
using byte = uint8_t;
static uint32_t frameTime = 0;
unsigned long millis() { return frameTime; }
long random(long bound) { return bound > 0 ? (frameTime * 1664525u + 1013904223u) % bound : 0; }
#include "vendor/FluxGarage_RoboEyes.h"
}
#undef DEFAULT
#undef TIRED
#undef ANGRY
#undef HAPPY
#undef ON
#undef OFF
#undef N
#undef NE
#undef E
#undef SE
#undef S
#undef SW
#undef W
#undef NW

namespace buddy {
namespace {
constexpr uint16_t background = 0x0863;
uint16_t blend(uint16_t a, uint16_t b, int weight) {
  weight = std::max(0, std::min(255, weight));
  auto channel = [&](int shift, int mask) { return (((a >> shift) & mask) * (255-weight) + ((b >> shift) & mask) * weight) / 255; };
  return (channel(11,31)<<11) | (channel(5,63)<<5) | channel(0,31);
}
constexpr char kFontChars[] = " 0123456789:%ABCDEFGHIJKLMNOPQRSTUVWXYZ-";
const uint8_t kPixelFont[][5] = {
  {0x00,0x00,0x00,0x00,0x00},
  {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
  {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
  {0x00,0x36,0x36,0x00,0x00}, {0x23,0x13,0x08,0x64,0x62},
  {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
  {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
  {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
  {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
  {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
  {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
  {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
  {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
  {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
  {0x08,0x08,0x08,0x08,0x08}
};
struct Surface {
  int w = 320, h = 240, top = 34;
  uint16_t color = 0x4E9F;
  std::vector<uint16_t> data = std::vector<uint16_t>(320*240, background);
  void pixel(int x, int y, uint16_t c) { if (x>=0 && x<w && y>=0 && y<h) data[y*w+x]=c; }
  void clearDisplay() { std::fill(data.begin(),data.end(),background); }
  void display() {}
  uint16_t ink(int y, uint8_t c) const { return c ? blend(color, 0xFFFF, std::max(0, 105-y/2)) : background; }
  void fillRoundRect(int x,int y,int width,int height,int radius,uint8_t c) {
    if(width<=0||height<=0)return;
    radius=std::max(0,std::min(radius,std::min(width,height)/2));
    for(int yy=std::max(0,y);yy<std::min(h-top,y+height);++yy) {
      int dy=yy<y+radius?y+radius-yy:yy>=y+height-radius?yy-(y+height-radius-1):0;
      int inset=dy?radius-static_cast<int>(std::sqrt(std::max(0,radius*radius-dy*dy))):0;
      for(int xx=std::max(0,x+inset);xx<std::min(w,x+width-inset);++xx) pixel(xx,yy+top,ink(yy,c));
    }
  }
  void fillTriangle(int ax,int ay,int bx,int by,int cx,int cy,uint8_t c) {
    auto edge=[](int x1,int y1,int x2,int y2,int x,int y){ return (x-x1)*(y2-y1)-(y-y1)*(x2-x1); };
    if(edge(ax,ay,bx,by,cx,cy)==0)return;
    for(int y=std::max(0,std::min({ay,by,cy}));y<=std::min(h-top-1,std::max({ay,by,cy}));++y)
      for(int x=std::max(0,std::min({ax,bx,cx}));x<=std::min(w-1,std::max({ax,bx,cx}));++x) {
        const auto a=edge(ax,ay,bx,by,x,y),b=edge(bx,by,cx,cy,x,y),d=edge(cx,cy,ax,ay,x,y);
        if((a>=0&&b>=0&&d>=0)||(a<=0&&b<=0&&d<=0))pixel(x,y+top,ink(y,c));
      }
  }
  void rect(int x,int y,int width,int height,uint16_t c) {
    for(int yy=y;yy<y+height;++yy)for(int xx=x;xx<x+width;++xx)pixel(xx,yy,c);
  }
  void text(const char* text,int x,int y,int scale,uint16_t c) {
    for(size_t i=0;text[i];++i){
      const char* p=std::strchr(kFontChars,text[i]); int glyph=p?int(p-kFontChars):0;
      for(int col=0;col<5;++col)for(int row=0;row<7;++row)
        if(kPixelFont[glyph][col]&(1<<row))rect(x+(i*6+col)*scale,y+row*scale,scale,scale,c);
    }
  }
};
}
struct BuddyDisplay::Impl {
  Surface screen;
  buddy_robo::RoboEyes<Surface> eyes{screen};
  bool portrait = false;
  bool initialized = false;
  uint16_t eyeColor = 0x4E9F;
  Mode lastMode = Mode::Idle;
  uint32_t lastSequence = 0;
  void setup() {
    screen.w=portrait?240:320; screen.h=portrait?320:240;
    eyes.begin(screen.w,screen.h-screen.top,20);
    const int ew=portrait?92:128, eh=portrait?186:156;
    eyes.setWidth(ew,ew); eyes.setHeight(eh,eh);
    eyes.setBorderradius(38,38); eyes.setSpacebetween(portrait?20:24);
    eyes.eyeLwidthCurrent=eyes.eyeRwidthCurrent=ew;
    eyes.spaceBetweenCurrent=portrait?20:24;
    eyes.eyeLx=eyes.eyeLxNext=(screen.w-2*ew-eyes.spaceBetweenCurrent)/2;
    eyes.eyeRx=eyes.eyeRxNext=eyes.eyeLx+ew+eyes.spaceBetweenCurrent;
    eyes.eyeLy=eyes.eyeRy=eyes.eyeLyNext=eyes.eyeRyNext=(screen.h-screen.top-eh)/2;
    eyes.open(); eyes.setAutoblinker(true,3,2); eyes.setCuriosity(true);
    initialized=true;
  }
};
BuddyDisplay::BuddyDisplay():impl_(new Impl) {}
BuddyDisplay::~BuddyDisplay() = default;
void BuddyDisplay::configure(bool portrait,uint16_t eyeColor) {
  if(impl_->portrait!=portrait)impl_->initialized=false;
  impl_->portrait=portrait; impl_->eyeColor=eyeColor;
}
void BuddyDisplay::render(const Snapshot& state,const LocalTime& time,int battery,uint32_t nowMs) {
  auto& p=*impl_; buddy_robo::frameTime=nowMs;
  if(!p.initialized)p.setup();
  p.screen.color=p.eyeColor;
  if(state.mode==Mode::Listening)p.screen.color=0x7FD4;
  if(state.mode==Mode::Reminder)p.screen.color=0xFD49;
  if(state.mode==Mode::SosCountdown||state.mode==Mode::SosActive)p.screen.color=0xF9EA;
  int mood=(state.mode==Mode::Idle && (time.hour>=22||time.hour<6))?1:0;
  if(state.mode==Mode::Speaking)mood=3;
  p.eyes.setMood(mood);
  if(state.speechSequence!=p.lastSequence && state.mode==Mode::Speaking)p.eyes.anim_laugh();
  p.lastSequence=state.speechSequence;
  int boundX=p.eyes.getScreenConstraint_X(),boundY=p.eyes.getScreenConstraint_Y();
  p.eyes.eyeLxNext=std::max(0,std::min(boundX,int(boundX/2+state.gazeX*boundX*.45f)));
  p.eyes.eyeLyNext=std::max(0,std::min(boundY,int(boundY/2+state.gazeY*boundY*.40f)));
  p.eyes.drawEyes();
  // Clock, date and battery are deliberately the only on-screen text.
  char clock[8],date[12],bat[8];
  static const char* months[]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
  std::snprintf(clock,sizeof(clock),"%02u:%02u",time.hour,time.minute);
  std::snprintf(date,sizeof(date),"%02u %s",time.day,months[time.month>=1&&time.month<=12?time.month-1:0]);
  if(time.year<2024){std::snprintf(clock,sizeof(clock),"--:--");std::snprintf(date,sizeof(date),"-- ---");}
  if(battery<0) std::snprintf(bat,sizeof(bat),"--%%");
  else std::snprintf(bat,sizeof(bat),"%d%%",std::max(0,std::min(100,battery)));
  int scale=p.portrait?1:2;
  p.screen.text(date,10,12,scale,0xCEFB);
  p.screen.text(clock,p.screen.w/2-29,9,2,0xFFFF);
  p.screen.text(bat,p.screen.w-int(std::strlen(bat))*6*scale-8,12,scale,0x9FF3);
  p.screen.rect(10,31,p.screen.w-20,1,0x2967);
  // Quiet, non-flashing listening indicator.
  if(state.mode==Mode::Listening)p.screen.rect(p.screen.w/2-18,p.screen.h-15,36,3,0x7FD4);
  // Steady red camera outline: visible in both the native and physical display.
  if(state.cameraActive){p.screen.rect(5,p.screen.h-18,12,9,0xF800);p.screen.rect(17,p.screen.h-16,4,5,0xF800);}
}
int BuddyDisplay::width()const{return impl_->screen.w;}
int BuddyDisplay::height()const{return impl_->screen.h;}
const std::vector<uint16_t>& BuddyDisplay::pixels()const{return impl_->screen.data;}
}
