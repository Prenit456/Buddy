#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace buddy_audio {
inline uint16_t le16(const uint8_t* p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline uint32_t le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline int16_t micSample(int32_t slot, unsigned shift) {
  return static_cast<int16_t>(std::max<int32_t>(-32768,
      std::min<int32_t>(32767, slot >> shift)));
}
// Both slots carry the same audio: the amplifier can select L, R or (L+R)/2.
// Multiplication avoids undefined signed left-shifts for negative samples.
inline void speakerFrame(int16_t sample, uint8_t volume, int32_t* frame) {
  const int32_t scaled=int32_t(sample)*int32_t(std::min<unsigned>(volume,100))/100;
  frame[0]=frame[1]=scaled*65536;
}

struct WaveInfo { uint32_t sampleRate=0, dataBytes=0; };

// Reader returns the actual byte count, including short network reads.
// Recognizes PCM mono 16-bit WAV with bounded metadata, including odd chunks.
template<class Reader>
bool readWaveHeader(Reader read, WaveInfo& info) {
  info={};
  auto exact=[&](uint8_t* p,size_t n) {
    while(n) { const size_t got=read(p,n); if(!got||got>n)return false; p+=got;n-=got; }
    return true;
  };
  uint8_t header[12];
  if(!exact(header,12)||std::memcmp(header,"RIFF",4)||std::memcmp(header+8,"WAVE",4))return false;
  bool haveFormat=false;
  uint8_t buffer[128];
  uint32_t metadata=0;
  for(unsigned chunks=0;chunks<32;++chunks) {
    uint8_t chunk[8];
    if(!exact(chunk,8))return false;
    const uint32_t length=le32(chunk+4);
    if(!std::memcmp(chunk,"data",4)) {
      if(!haveFormat||!length||(length&1)||length>info.sampleRate*2*90)return false;
      info.dataBytes=length;
      return true;
    }
    if(length>65536 || metadata+length>65536)return false;
    metadata+=length;
    uint32_t remaining=length+(length&1);
    if(!std::memcmp(chunk,"fmt ",4)) {
      if(haveFormat||length<16||!exact(buffer,16))return false;
      info.sampleRate=le32(buffer+4);
      if(le16(buffer)!=1||le16(buffer+2)!=1||le16(buffer+14)!=16||
         le16(buffer+12)!=2||info.sampleRate<8000||info.sampleRate>48000||
         le32(buffer+8)!=info.sampleRate*2)return false;
      haveFormat=true;
      remaining-=16;
    }
    while(remaining) {
      const size_t n=std::min<size_t>(remaining,sizeof(buffer));
      if(!exact(buffer,n))return false;
      remaining-=n;
    }
  }
  return false;
}
}  // namespace buddy_audio
