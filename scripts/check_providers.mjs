// Explicit live check: synthetic speech and one synthetic conversation only.
// Uses provider credits; all test accounts/state live in a disposable directory.
import {spawn} from 'node:child_process';
import {mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {setTimeout as pause} from 'node:timers/promises';
import assert from 'node:assert/strict';

const root=dirname(dirname(fileURLToPath(import.meta.url)));
const directory=await mkdtemp(join(tmpdir(),'buddy-provider-check-'));
const conversationOnly=process.argv.includes('--conversation-only');
let server,base;
const results=[];
// Gemini returns 24 kHz; the physical microphone sends 16 kHz. Reproduce the
// microphone's real transport format when feeding synthetic speech to STT.
function captureWave(wav) {
  let rate=0,pcm;
  for(let at=12;at+8<=wav.length;) {
    const id=wav.toString('ascii',at,at+4),length=wav.readUInt32LE(at+4);
    assert.ok(at+8+length<=wav.length);
    if(id==='fmt ') {
      assert.equal(wav.readUInt16LE(at+8),1);
      assert.equal(wav.readUInt16LE(at+10),1);
      assert.equal(wav.readUInt16LE(at+22),16);
      rate=wav.readUInt32LE(at+12);
    }
    if(id==='data')pcm=wav.subarray(at+8,at+8+length);
    at+=8+length+(length&1);
  }
  assert.ok(rate&&pcm?.length);
  const samples=Math.floor(pcm.length/2*16000/rate);
  const result=Buffer.alloc(44+samples*2);
  result.write('RIFF');result.writeUInt32LE(result.length-8,4);result.write('WAVEfmt ',8);
  result.writeUInt32LE(16,16);result.writeUInt16LE(1,20);result.writeUInt16LE(1,22);
  result.writeUInt32LE(16000,24);result.writeUInt32LE(32000,28);
  result.writeUInt16LE(2,32);result.writeUInt16LE(16,34);
  result.write('data',36);result.writeUInt32LE(samples*2,40);
  for(let i=0;i<samples;++i) {
    const position=i*rate/16000,index=Math.floor(position),fraction=position-index;
    const a=pcm.readInt16LE(index*2),b=pcm.readInt16LE(Math.min(index+1,pcm.length/2-1)*2);
    result.writeInt16LE(Math.round(a+(b-a)*fraction),44+2*i);
  }
  return result;
}
async function start(fallback=false) {
  let log='';
  server=spawn(process.execPath,['care-hub/server.mjs'],{
    cwd:root,env:{...process.env,PORT:'0',BUDDY_HOST:'127.0.0.1',
      BUDDY_STATE_DIR:directory,BUDDY_SKIP_ENV:'0',
      BUDDY_TLS_CERT:'',BUDDY_TLS_KEY:'',...(fallback?{
        ELEVENLABS_API_KEY:'disabled-for-google-fallback-check',
        ELEVENLABS_API_KEYS:'disabled-backup-for-google-fallback-check'
      }:{})},
    stdio:['ignore','pipe','pipe']
  });
  server.stdout.on('data',b=>log+=b);server.stderr.on('data',b=>log+=b);
  for(let i=0;i<150;++i) {
    const match=/Buddy Care:\s+http:\/\/localhost:(\d+)/.exec(log);
    if(match){base='http://127.0.0.1:'+match[1];return;}
    if(server.exitCode!==null)throw Error('Disposable Care Hub exited before startup');
    await pause(50);
  }
  throw Error('Disposable Care Hub did not start');
}
async function stop() {
  if(server&&server.exitCode===null) {
    const exit=new Promise(resolve=>server.once('exit',resolve));
    server.kill('SIGTERM');await exit;
  }
}
async function post(path,data,token) {
  return fetch(base+'/api/v2'+path,{
    method:'POST',headers:{'content-type':'application/json',...(token?{authorization:'Bearer '+token}:{})},
    body:JSON.stringify(data),signal:AbortSignal.timeout(110000)
  });
}
async function json(response) {
  const data=await response.json();
  if(!response.ok)throw Error('HTTP '+response.status+': '+String(data.error||'request failed').slice(0,240));
  return data;
}
async function check(name,action) {
  try {const detail=await action();results.push({check:name,ok:true,...detail});}
  catch(error){results.push({check:name,ok:false,error:error.message});}
  console.log(JSON.stringify(results.at(-1)));
}
try {
  await start();
  const account=await json(await post('/auth/register',{
    name:'Firmware Check',role:'user',email:'firmware-check@example.test',password:'Temporary-check-only-92848'
  }));
  const me=await (await fetch(base+'/api/v2/me',{headers:{authorization:'Bearer '+account.token}})).json();
  const device=await json(await post('/device/enroll',{name:'Firmware check adapter',capabilities:{audio:true}}));
  await json(await post('/circles/'+me.circles[0].id+'/devices/claim',{code:device.pairCode},account.token));
  let wav;
  if(!conversationOnly)await check('ElevenLabs primary speech through authenticated device API',async()=>{
    const response=await post('/device/tts',{
      text:'Hey Buddy, what is the time?',voiceId:'bella',language:'en-IN',rate:72,format:'wav'
    },device.deviceToken);
    if(!response.ok){await json(response);}
    wav=Buffer.from(await response.arrayBuffer());
    assert.equal(wav.toString('ascii',0,4),'RIFF');
    assert.equal(response.headers.get('x-buddy-tts-engine'),'elevenlabs');
    return {engine:response.headers.get('x-buddy-tts-engine'),model:response.headers.get('x-buddy-tts-model'),bytes:wav.length};
  });
  if(!conversationOnly)await check('ElevenLabs transcription through authenticated device API',async()=>{
    assert.ok(wav,'No WAV available from the speech check');
    const result=await json(await post('/device/transcribe',{audio:'data:audio/wav;base64,'+captureWave(wav).toString('base64')},device.deviceToken));
    assert.match(result.text,/hey buddy/i);
    return {recognizedWakePhrase:true};
  });
  await check('OpenRouter conversation through authenticated device API',async()=>{
    const result=await json(await post('/device/assistant',{message:'Say hello in one short sentence.'},device.deviceToken));
    assert.ok(typeof result.answer==='string'&&result.answer.trim().length);
    return {receivedAnswer:true};
  });
  if(!conversationOnly){
  await stop();
  await start(true);
  await check('Google speech fallback after all ElevenLabs keys fail',async()=>{
    const response=await post('/device/tts',{
      text:'Buddy is ready.',voiceId:'bella',language:'en-IN',rate:72,format:'wav'
    },device.deviceToken);
    if(!response.ok){await json(response);}
    const audio=Buffer.from(await response.arrayBuffer());
    assert.equal(audio.toString('ascii',0,4),'RIFF');
    const engine=response.headers.get('x-buddy-tts-engine');
    assert.equal(engine,'gemini-tts','Expected Google fallback; received '+engine);
    return {engine,bytes:audio.length};
  });
  }
} finally {
  await stop();
  await rm(directory,{recursive:true,force:true});
}
if(results.some(r=>!r.ok))process.exitCode=1;
