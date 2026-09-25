#include "VoiceGateway.h"
#include "HardwareConfig.h"
#include "HubHttp.h"
#if BUDDY_ENABLE_NETWORK_VOICE
#include <driver/i2s.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <esp_heap_caps.h>
#include <PcmAudio.h>
#include <cmath>
#include <algorithm>
#include <vector>

#if BUDDY_ENABLE_SAM
#error "SAM and VoiceGateway cannot own the same I2S bus"
#endif

namespace {
constexpr i2s_port_t kAudioPort=I2S_NUM_0;
constexpr uint32_t kCaptureRate=16000;
constexpr size_t kDmaFrames=256, kDmaBuffers=8;

bool audioRate(uint32_t rate) {
  return i2s_set_clk(kAudioPort,rate,I2S_BITS_PER_SAMPLE_32BIT,I2S_CHANNEL_STEREO)==ESP_OK;
}
void discardMic() {
  int32_t raw[kDmaFrames*2];
  size_t bytes=0;
  for(size_t n=0;n<kDmaBuffers+1;++n) {
    i2s_read(kAudioPort,raw,sizeof(raw),&bytes,0);
    if(!bytes)break;
  }
}
size_t readMic(int16_t* output,size_t capacity,TickType_t wait) {
  int32_t raw[kDmaFrames*2];
  const size_t wanted=std::min(capacity,kDmaFrames);
  size_t bytes=0;
  if(i2s_read(kAudioPort,raw,wanted*8,&bytes,wait)!=ESP_OK)return 0;
  const size_t frames=bytes/8;
  for(size_t i=0;i<frames;++i)output[i]=buddy_audio::micSample(raw[2*i],buddy_hw::kMicShift);
  return frames;
}
String base64(const uint8_t* raw,size_t length) {
  const size_t capacity=4*((length+2)/3)+1;
  auto* buffer=static_cast<unsigned char*>(malloc(capacity));
  if(!buffer)return "";
  size_t written=0;
  String result;
  if(mbedtls_base64_encode(buffer,capacity,&written,raw,length)==0) {
    buffer[written]=0;result=reinterpret_cast<char*>(buffer);
  }
  free(buffer);
  return result;
}
void waveHeader(uint8_t* raw,size_t samples) {
  memset(raw,0,44);
  auto put=[&](size_t at,uint32_t value) {for(int i=0;i<4;++i)raw[at+i]=(value>>(8*i))&255;};
  memcpy(raw,"RIFF",4);put(4,samples*2+36);memcpy(raw+8,"WAVEfmt ",8);
  put(16,16);raw[20]=raw[22]=1;put(24,kCaptureRate);put(28,kCaptureRate*2);
  raw[32]=2;raw[34]=16;memcpy(raw+36,"data",4);put(40,samples*2);
}
}

void VoiceGateway::begin() {
  mutex_=xSemaphoreCreateMutex();
  jobs_=xQueueCreate(5,sizeof(Job));results_=xQueueCreate(5,sizeof(Job));
  frames_=xQueueCreate(1,sizeof(String*));
  if(!mutex_||!jobs_||!results_||!frames_) {
    Serial.println("Voice queues could not be allocated");return;
  }
  // One master clocks BOTH peripherals. 32-bit stereo slots preserve the
  // microphone's 24-bit words and provide 64 BCLK pulses per audio frame.
  i2s_config_t config={};
  config.mode=static_cast<i2s_mode_t>(I2S_MODE_MASTER|I2S_MODE_RX|I2S_MODE_TX);
  config.sample_rate=kCaptureRate;
  config.bits_per_sample=I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format=I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format=I2S_COMM_FORMAT_STAND_I2S;
  config.dma_buf_count=kDmaBuffers;config.dma_buf_len=kDmaFrames;
  config.tx_desc_auto_clear=true;
  i2s_pin_config_t pins={};
  pins.mck_io_num=I2S_PIN_NO_CHANGE;
  pins.bck_io_num=buddy_hw::kI2sBclkPin;pins.ws_io_num=buddy_hw::kI2sLrcPin;
  pins.data_out_num=buddy_hw::kI2sDataOutPin;pins.data_in_num=buddy_hw::kMicDataPin;
  esp_err_t error=i2s_driver_install(kAudioPort,&config,0,nullptr);
  if(error!=ESP_OK){Serial.printf("I2S install: %s\n",esp_err_to_name(error));return;}
  error=i2s_set_pin(kAudioPort,&pins);
  if(error!=ESP_OK){Serial.printf("I2S pins: %s\n",esp_err_to_name(error));i2s_driver_uninstall(kAudioPort);return;}
  i2s_zero_dma_buffer(kAudioPort);
  camera_.begin();
  ready_=true;
  if(xTaskCreatePinnedToCore(task,"buddy-voice",20480,this,1,nullptr,0)!=pdPASS) {
    ready_=false;i2s_driver_uninstall(kAudioPort);
  }
  Serial.printf("Shared I2S audio: %s; camera: %s\n",ready_?"ready":"failed",camera_.ready()?"ready":"unavailable");
}
void VoiceGateway::configure(const String& url,const String& voice,const String& language,uint8_t rate,uint8_t volume) {
  if(!mutex_)return;
  xSemaphoreTake(mutex_,portMAX_DELAY);
  url_=url;while(url_.endsWith("/"))url_.remove(url_.length()-1);
  voice_=voice;language_=language;rate_=rate;volume_=std::min<unsigned>(volume,100);
  configured_=url_.startsWith("http://")||url_.startsWith("https://");
  xSemaphoreGive(mutex_);
}
bool VoiceGateway::enabled()const{return ready_&&configured_;}
bool VoiceGateway::audioReady()const{return ready_;}
bool VoiceGateway::cameraReady()const{return camera_.ready()&&ready_;}
void VoiceGateway::configureHub(const String& base,const String& token) {
  if(!mutex_)return;
  xSemaphoreTake(mutex_,portMAX_DELAY);
  if(hub_!=base||token_!=token)++generation_;
  hub_=base;token_=token;configured_=token.length()&&base.length();
  xSemaphoreGive(mutex_);
}
void VoiceGateway::startCall(const String& id,const String& kind,uint32_t duration) {
  if(!ready_||(kind!="audio"&&!camera_.ready()))return;
  ++generation_;xQueueReset(jobs_);
  xSemaphoreTake(mutex_,portMAX_DELAY);
  callId_=id;callKind_=kind;
  callUntil_=millis()+std::min<uint32_t>(duration,kind=="camera"?120000:1800000);
  xSemaphoreGive(mutex_);
}
void VoiceGateway::stopCall() {
  callUntil_=0;++generation_;
  if(jobs_)xQueueReset(jobs_);
  String* frame=nullptr;
  while(frames_&&xQueueReceive(frames_,&frame,0)==pdTRUE)delete frame;
}
bool VoiceGateway::inCall()const{return callUntil_&&int32_t(millis()-callUntil_)<0;}
bool VoiceGateway::cameraActive()const {
  if(!mutex_||!inCall())return false;
  xSemaphoreTake(mutex_,portMAX_DELAY);
  const bool active=callKind_!="audio";
  xSemaphoreGive(mutex_);
  return active;
}
bool VoiceGateway::receiveFrame(String& jpeg) {
  String* frame=nullptr;
  if(!frames_||xQueueReceive(frames_,&frame,0)!=pdTRUE)return false;
  jpeg=*frame;delete frame;return true;
}
void VoiceGateway::speak(const String& text,bool urgent) {
  if(!enabled()||inCall())return;
  if(urgent){
    ++generation_;xQueueReset(jobs_);
    Job cue={};cue.kind=2;cue.text[0]='1';xQueueSend(jobs_,&cue,0);
  }
  Job job={};strlcpy(job.text,text.c_str(),sizeof(job.text));
  if(xQueueSend(jobs_,&job,0)!=pdTRUE)Serial.println("Speech queue full");
}
void VoiceGateway::chime(bool sos) {
  if(!ready_)return;
  if(sos)++generation_;
  Job job={};job.kind=2;job.text[0]=sos?'1':'0';
  xQueueSendToFront(jobs_,&job,0);
}
void VoiceGateway::ask(const String& text) {
  if(!enabled()||inCall())return;
  Job job={};job.kind=1;strlcpy(job.text,text.c_str(),sizeof(job.text));
  if(xQueueSend(jobs_,&job,0)!=pdTRUE)reportError("The voice queue is busy. Please try again.");
}
bool VoiceGateway::receive(String& text,uint8_t& kind) {
  if(!results_)return false;
  Job job;
  if(xQueueReceive(results_,&job,0)!=pdTRUE)return false;
  text=job.text;kind=job.kind;return true;
}
void VoiceGateway::reportError(const char* message) {
  Serial.println(message);
  if(!results_)return;
  Job reply={};reply.kind=2;strlcpy(reply.text,message,sizeof(reply.text));
  xQueueSend(results_,&reply,0);
}
void VoiceGateway::task(void* self){static_cast<VoiceGateway*>(self)->run();}

bool VoiceGateway::playPcm(const uint8_t* pcm,size_t bytes,uint8_t volume,uint32_t generation) {
  if(bytes&1)return false;
  int32_t slots[256];
  while(bytes && generation==generation_) {
    const size_t samples=std::min<size_t>(bytes/2,128);
    for(size_t i=0;i<samples;++i)
      buddy_audio::speakerFrame(static_cast<int16_t>(buddy_audio::le16(pcm+2*i)),volume,slots+2*i);
    size_t left=samples*8;
    const uint8_t* at=reinterpret_cast<const uint8_t*>(slots);
    while(left && generation==generation_) {
      size_t written=0;
      if(i2s_write(kAudioPort,at,left,&written,pdMS_TO_TICKS(250))!=ESP_OK||!written)return false;
      left-=written;at+=written;
    }
    pcm+=samples*2;bytes-=samples*2;
  }
  return bytes==0 && generation==generation_;
}
void VoiceGateway::playChime(bool sos,uint8_t volume,uint32_t generation) {
  audioRate(kCaptureRate);
  for(unsigned note=0;note<(sos?3u:2u)&&generation==generation_;++note) {
    const double frequency=sos?(note%2?740:980):(note?880:660);
    constexpr size_t frames=2400;
    int16_t pcm[128];
    for(size_t n=0;n<frames&&generation==generation_;n+=128) {
      const size_t count=std::min<size_t>(128,frames-n);
      for(size_t i=0;i<count;++i) {
        const double envelope=std::min(1.0,std::min(double(n+i)/160.0,double(frames-n-i)/160.0));
        pcm[i]=int16_t(6000.0*envelope*std::sin(6.28318530718*frequency*(n+i)/kCaptureRate));
      }
      if(!playPcm(reinterpret_cast<const uint8_t*>(pcm),count*2,volume,generation))break;
    }
  }
  // Flush queued audio with silence rather than cutting off the final syllable.
  uint8_t silence[512]={};
  for(unsigned n=0;n<kDmaBuffers+1&&generation==generation_;++n)playPcm(silence,sizeof(silence),0,generation);
  i2s_zero_dma_buffer(kAudioPort);discardMic();
}

void VoiceGateway::run() {
  constexpr size_t maxSamples=128000,preRollSamples=1600;
  auto* raw=static_cast<uint8_t*>(heap_caps_malloc(maxSamples*2+44,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!raw)raw=static_cast<uint8_t*>(malloc(maxSamples*2+44));
  if(!raw){ready_=false;reportError("Not enough memory for microphone capture");vTaskDelete(nullptr);return;}
  auto* pcm=reinterpret_cast<int16_t*>(raw+44);
  int16_t input[kDmaFrames],preRoll[preRollSamples]={};
  size_t used=0,silence=0,voicedSamples=0,preAt=0,preCount=0;
  uint32_t retryAt=0;
  unsigned failures=0;
  bool wasInCall=false,wasConnected=false;
  for(;;) {
    String base,voice,language,hub,token,callId,callKind;
    uint8_t pace,volume;
    xSemaphoreTake(mutex_,portMAX_DELAY);
    base=url_;voice=voice_;language=language_;pace=rate_;volume=volume_;
    hub=hub_;token=token_;callId=callId_;callKind=callKind_;
    xSemaphoreGive(mutex_);
    if(inCall()) {
      used=silence=voicedSamples=preAt=preCount=0;wasInCall=true;
      mediaStep(hub,token,callId,callKind,volume);
      continue;
    }
    camera_.stop();
    if(wasInCall){wasInCall=false;audioRate(kCaptureRate);i2s_zero_dma_buffer(kAudioPort);discardMic();}
    const String service=token.length()?hub+"/api/v2/device":base+"/api";
    if(token.length())base=hub;
    const bool connected=enabled()&&WiFi.status()==WL_CONNECTED;
    if(connected&&!wasConnected)discardMic();
    wasConnected=connected;
    Job job;
    if(xQueueReceive(jobs_,&job,0)==pdTRUE) {
      const uint32_t generation=generation_;
      used=silence=voicedSamples=preAt=preCount=0;
      if(job.kind==2){playChime(job.text[0]=='1',volume,generation);continue;}
      if(!connected){playChime(false,volume,generation);continue;}
      bool success=false;
      HTTPClient http;WiFiClient client;WiFiClientSecure secure;
      // WAV is read from the underlying stream; HTTP/1.0 avoids raw chunk framing.
      http.useHTTP10(true);
      if(beginHubHttp(http,client,secure,service+(job.kind?"/assistant":"/tts"),token)) {
        http.setTimeout(60000); // Allows Gemini's timeout and the ElevenLabs fallback.
        DynamicJsonDocument body(2048);
        if(job.kind)body["message"]=job.text;
        else{body["text"]=job.text;body["voiceId"]=voice;body["language"]=language;body["rate"]=pace;body["format"]="wav";}
        String payload;serializeJson(body,payload);
        const int code=http.POST(payload);
        if(generation!=generation_){http.end();continue;}
        if(code==200&&job.kind) {
          DynamicJsonDocument answer(4096);
          if(!deserializeJson(answer,http.getString())&&answer["answer"].is<const char*>()) {
            Job reply={};reply.kind=1;
            strlcpy(reply.text,answer["answer"]|"",sizeof(reply.text));
            success=xQueueSend(results_,&reply,0)==pdTRUE;
          }
        } else if(code==200) {
          auto* stream=http.getStreamPtr();stream->setTimeout(10000);
          buddy_audio::WaveInfo info;
          auto read=[&](uint8_t* data,size_t length)->size_t {
            return generation==generation_?stream->readBytes(data,length):0;
          };
          if(buddy_audio::readWaveHeader(read,info)&&audioRate(info.sampleRate)) {
            uint8_t buffer[512];uint32_t remaining=info.dataBytes;
            const uint32_t started=millis();
            success=true;
            while(remaining&&generation==generation_&&millis()-started<90000) {
              const size_t wanted=std::min<size_t>(remaining,sizeof(buffer));
              size_t got=0;
              while(got<wanted&&generation==generation_) {
                const size_t n=read(buffer+got,wanted-got);if(!n)break;got+=n;
              }
              if(got!=wanted||!playPcm(buffer,got,volume,generation)){success=false;break;}
              remaining-=got;
            }
            success=success&&!remaining;
            memset(buffer,0,sizeof(buffer));
            for(unsigned n=0;n<kDmaBuffers+1&&generation==generation_;++n)
              playPcm(buffer,sizeof(buffer),0,generation);
          }
          // The microphone and amplifier share clocks: restore capture timing.
          i2s_zero_dma_buffer(kAudioPort);audioRate(kCaptureRate);
        }
        if(!success&&generation==generation_)Serial.printf("Voice request failed, HTTP %d\n",code);
        http.end();
      }
      if(!success&&generation==generation_) {
        reportError("The voice service is unavailable. Check the Care Hub and provider account.");
        playChime(false,volume,generation);
      }
      discardMic();
      continue;
    }
    if(!connected||int32_t(millis()-retryAt)<0) {
      used=silence=voicedSamples=preAt=preCount=0;
      discardMic();vTaskDelay(pdMS_TO_TICKS(50));continue;
    }
    const size_t count=readMic(input,kDmaFrames,pdMS_TO_TICKS(30));
    if(!count){vTaskDelay(pdMS_TO_TICKS(5));continue;}
    double level=0;
    for(size_t i=0;i<count;++i)level+=double(input[i])*input[i];
    const bool voiced=std::sqrt(level/count)>buddy_hw::kVadThreshold;
    if(!used&&voiced) {
      const size_t first=(preAt+preRollSamples-preCount)%preRollSamples;
      for(size_t i=0;i<preCount;++i)pcm[used++]=preRoll[(first+i)%preRollSamples];
    }
    if(voiced||used) {
      if(voiced){silence=0;voicedSamples+=count;}else silence+=count;
      for(size_t i=0;i<count&&used<maxSamples;++i)pcm[used++]=input[i];
    }
    for(size_t i=0;i<count;++i) {
      preRoll[preAt]=input[i];preAt=(preAt+1)%preRollSamples;
      preCount=std::min(preCount+1,preRollSamples);
    }
    if(used&&(silence>11200||used==maxSamples)) {
      if(voicedSamples>=1600) {
        waveHeader(raw,used);
        const String encoded=base64(raw,used*2+44);
        bool success=false;
        HTTPClient http;WiFiClient client;WiFiClientSecure secure;
        const uint32_t generation=generation_;
        if(encoded.length()&&beginHubHttp(http,client,secure,service+"/transcribe",token)) {
          http.setTimeout(30000);
          String payload="{\"audio\":\"data:audio/wav;base64,"+encoded+"\"}";
          const int code=http.POST(payload);
          if(code==200&&generation==generation_&&!inCall()) {
            DynamicJsonDocument result(2048);
            if(!deserializeJson(result,http.getString())&&result["text"].is<const char*>()) {
              Job heard={};strlcpy(heard.text,result["text"]|"",sizeof(heard.text));
              if(heard.text[0])xQueueSend(results_,&heard,0);
              success=true;
            }
          }
          http.end();
        }
        if(generation==generation_) {
          if(success)failures=0;
          else {
            ++failures;
            retryAt=millis()+std::min<unsigned>(60000,failures*10000);
            if(failures==1||failures%6==0)
              reportError("Speech recognition failed; retrying with a delay. Check ElevenLabs on the Care Hub.");
          }
        }
        discardMic();
      }
      used=silence=voicedSamples=preAt=preCount=0;
    }
  }
}

void VoiceGateway::mediaStep(const String& base,const String& token,const String& callId,const String& kind,uint8_t volume) {
  static String previous;
  static uint32_t after=0,frameAfter=0,lastFrame=0;
  const uint32_t generation=generation_;
  if(previous!=callId) {
    previous=callId;after=frameAfter=lastFrame=0;
    audioRate(kCaptureRate);i2s_zero_dma_buffer(kAudioPort);discardMic();
  }
  DynamicJsonDocument body(100000);
  body["callId"]=callId;body["after"]=after;body["frameAfter"]=frameAfter;
  if(kind!="camera") {
    std::vector<int16_t> pcm(2048);
    size_t samples=0;
    do {
      const size_t got=readMic(pcm.data()+samples,pcm.size()-samples,samples?0:pdMS_TO_TICKS(40));
      if(!got)break;
      samples+=got;
    }while(samples<pcm.size());
    if(samples)body["audio"]=base64(reinterpret_cast<const uint8_t*>(pcm.data()),samples*2);
  }
  if(kind!="audio"&&millis()-lastFrame>600) {
    const String jpeg=camera_.jpeg();
    if(jpeg.length())body["jpeg"]=jpeg;
    lastFrame=millis();
  }
  if(!inCall()||generation!=generation_)return;
  HTTPClient http;WiFiClient plain;WiFiClientSecure secure;
  if(beginHubHttp(http,plain,secure,base+"/api/v2/device/media",token)) {
    http.setTimeout(4000);
    String payload;serializeJson(body,payload);
    const int code=http.POST(payload);
    if(code==200&&inCall()&&generation==generation_) {
      DynamicJsonDocument response(300000);
      if(!deserializeJson(response,http.getString())) {
        for(JsonObjectConst chunk:response["audio"].as<JsonArrayConst>()) {
          after=chunk["seq"]|after;
          if(kind=="camera")continue;
          const char* encoded=chunk["audio"]|"";
          const size_t length=strlen(encoded);
          if(length>22000)continue;
          auto* data=static_cast<uint8_t*>(malloc(length*3/4+4));
          size_t written=0;
          if(data&&mbedtls_base64_decode(data,length*3/4+4,&written,
                reinterpret_cast<const unsigned char*>(encoded),length)==0&&inCall())
            playPcm(data,written,volume,generation);
          free(data);
        }
        if(kind=="video"&&response["jpeg"].is<const char*>()&&inCall()&&generation==generation_) {
          frameAfter=response["frameSeq"]|frameAfter;
          auto* jpeg=new String(response["jpeg"].as<String>());
          String* old=nullptr;
          if(xQueueReceive(frames_,&old,0)==pdTRUE)delete old;
          if(xQueueSend(frames_,&jpeg,0)!=pdTRUE)delete jpeg;
        }
      }
    }else if(code!=200) {
      stopCall();reportError("The call connection ended. Ask your circle to call again.");
    }
    http.end();
  }else stopCall();
  vTaskDelay(pdMS_TO_TICKS(kind=="camera"?150:5));
}
#else
void VoiceGateway::begin(){}
void VoiceGateway::configure(const String&,const String&,const String&,uint8_t,uint8_t){}
void VoiceGateway::speak(const String&,bool){}
void VoiceGateway::chime(bool){}
void VoiceGateway::ask(const String&){}
bool VoiceGateway::receive(String&,uint8_t&){return false;}
bool VoiceGateway::enabled()const{return false;}
bool VoiceGateway::audioReady()const{return false;}
bool VoiceGateway::cameraReady()const{return false;}
void VoiceGateway::configureHub(const String&,const String&){}
void VoiceGateway::startCall(const String&,const String&,uint32_t){}
void VoiceGateway::stopCall(){}
bool VoiceGateway::inCall()const{return false;}
bool VoiceGateway::cameraActive()const{return false;}
bool VoiceGateway::receiveFrame(String&){return false;}
#endif
