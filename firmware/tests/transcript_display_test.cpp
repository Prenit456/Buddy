#include <cassert>
#include <cstdint>
#include <iostream>

#include <BuddyDisplay.h>

namespace {
size_t whitePixelsBelow(const buddy::BuddyDisplay& display, int top) {
  size_t count=0;
  const auto& pixels=display.pixels();
  for(int y=top;y<display.height();++y)
    for(int x=0;x<display.width();++x)
      if(pixels[y*display.width()+x]==0xFFFF)++count;
  return count;
}
size_t eyePixelsInBand(const buddy::BuddyDisplay& display,int top,int bottom) {
  size_t count=0;
  const auto& pixels=display.pixels();
  for(int y=top;y<bottom;++y)
    for(int x=25;x<display.width()-25;++x)
      if(pixels[y*display.width()+x]!=0x0863)++count;
  return count;
}
}

int main() {
  buddy::BuddyDisplay display;
  const buddy::Snapshot state;
  const buddy::LocalTime time{2026,9,25,5,14,30,0};

  for(uint32_t now=1000;now<=1450;now+=50)display.render(state,time,-1,now);
  assert(eyePixelsInBand(display,166,175)>100);
  display.showTranscript("Hey, tell me a short story about a friendly robot",1500);
  for(uint32_t now=1500;now<=2000;now+=50)display.render(state,time,-1,now);
  assert(display.width()==320 && display.height()==240);
  assert(eyePixelsInBand(display,166,175)==0);
  assert(whitePixelsBelow(display,197)>20);
  display.render(state,time,-1,13501);
  assert(whitePixelsBelow(display,197)==0);

  display.configure(true,0x4E9F);
  for(uint32_t now=13550;now<=13950;now+=50)display.render(state,time,-1,now);
  assert(eyePixelsInBand(display,220,235)>100);
  display.showTranscript("Hey, can you remind me to drink water?",14000);
  for(uint32_t now=14000;now<=14500;now+=50)display.render(state,time,-1,now);
  assert(display.width()==240 && display.height()==320);
  assert(eyePixelsInBand(display,220,235)==0);
  assert(whitePixelsBelow(display,260)>20);
  std::cout << "Transcript caption renders below raised eyes in both orientations: OK\n";
}
