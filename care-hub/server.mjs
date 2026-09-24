import { createServer } from "node:http";
import { readFile, writeFile, mkdir, stat, mkdtemp, rm } from "node:fs/promises";
import { createReadStream, existsSync } from "node:fs";
import { execFile } from "node:child_process";
import { dirname, extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";
import { tmpdir } from "node:os";
import { promisify } from "node:util";
import { prescriptionDraft, reviewedReminders } from './care.mjs';
import { CareHub } from './care-hub.mjs';
import { SupabaseState } from './supabase-state.mjs';
import {nvidiaChatKeys, nvidiaConversation, DEFAULT_NVIDIA_CHAT_MODEL} from './conversation-provider.mjs';
import {configuredElevenLabsKeys, ElevenLabsKeyRing, isElevenLabsLimitError} from './elevenlabs-keys.mjs';
import { createServer as createSecureServer } from 'node:https';

const here = dirname(fileURLToPath(import.meta.url));
const root = dirname(here);
const appRoot = join(root, "companion-app");
const stateDirectory = process.env.BUDDY_STATE_DIR || join(here,"state");
const port = Number(process.env.PORT || 4173);
const execFileAsync = promisify(execFile);
let settings;
let careHub;
let elevenLabsKeys;
let nvidiaKeys;


async function loadLocalEnv() {
  if(process.env.BUDDY_SKIP_ENV==='1')return;
  try {
    const source = await readFile(join(root, ".env.local"), "utf8");
    for (const rawLine of source.split(/\r?\n/)) {
      const line = rawLine.trim();
      if (!line || line.startsWith("#")) continue;
      const equals = line.indexOf("=");
      if (equals < 1) continue;
      const key = line.slice(0, equals).trim();
      let value = line.slice(equals + 1).trim();
      if ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith("'") && value.endsWith("'"))) value = value.slice(1, -1);
      if (!process.env[key]) process.env[key] = value;
    }
  } catch (error) {
    if (error.code !== "ENOENT") throw error;
  }
}

async function loadSettings() {
  settings = JSON.parse(await readFile(join(here, "default-settings.json"), "utf8"));
  const migratedVoices = { warm: "bella", gentle: "sarah", bright: "bella", robot: "brian", grounded: "brian", tara: "bella", rishi: "brian", samantha: "sarah" };
  if (migratedVoices[settings.voice?.voiceId]) settings.voice.voiceId = migratedVoices[settings.voice.voiceId];
  settings.device = { callName:'Buddy', timezone:'Asia/Kolkata', ...settings.device };
  settings.display = { orientation:'landscape', eyeColor:20127, ...settings.display };
  settings.preferences = { ...settings.preferences, privacyMode:'wake-phrase' };
}

function localDate(date = new Date()) {
  return new Intl.DateTimeFormat('en-CA', {timeZone:settings.device?.timezone || 'Asia/Kolkata',year:'numeric',month:'2-digit',day:'2-digit'}).format(date);
}

function json(res, code, value) {
  const body = JSON.stringify(value);
  res.writeHead(code, { "content-type": "application/json; charset=utf-8", "cache-control": "no-store", "access-control-allow-origin": "*", "access-control-allow-headers": "content-type" });
  res.end(body);
}

async function bodyJson(req) {
  const chunks = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > (/(prescription|transcribe)/.test(req.url)?8*1024*1024:512*1024)) throw Object.assign(new Error("Request is too large"),{status:413});
    chunks.push(chunk);
  }
  return JSON.parse(Buffer.concat(chunks).toString("utf8") || "{}");
}

function callConversation(message, history = [], careSettings = settings) {
  return nvidiaConversation({message, history, careSettings,
    keys:nvidiaKeys.orderedKeys(),
    model:process.env.NVIDIA_CHAT_MODEL || DEFAULT_NVIDIA_CHAT_MODEL,
    onKeyRejected:key => nvidiaKeys.moveToLast(key)});
}

const voiceProfiles = {
  bella: { label: "Bella", systemVoice: "Samantha", geminiVoice:"Kore", elevenVoice: "hpp4J3VqNfWAUOO0d1Us", nimVoice: "Magpie-Multilingual.EN-US.Aria.Calm", rateOffset: -8 },
  sarah: { label: "Sarah", systemVoice: "Samantha", geminiVoice:"Aoede", elevenVoice: "EXAVITQu4vr4xnSDxMaL", nimVoice: "Magpie-Multilingual.EN-US.Aria.Neutral", rateOffset: -10 },
  brian: { label: "Brian", systemVoice: "Rishi", geminiVoice:"Fenrir", elevenVoice: "nPczCjzI2devNBz1zQrb", nimVoice: "Magpie-Multilingual.EN-US.Jason.Calm", rateOffset: -8 },
  daniel: { label: "Daniel", systemVoice: "Daniel", geminiVoice:"Puck", elevenVoice: "onwK4e9ZLuTAKqWW03F9", nimVoice: "Magpie-Multilingual.EN-US.Jason.Neutral", rateOffset: -5 }
};

function resolveVoice(voiceId, language) {
  const aliases = { warm: "bella", gentle: "sarah", bright: "bella", robot: "brian", grounded: "brian", tara: "bella", rishi: "brian", samantha: "sarah" };
  const profile = voiceProfiles[aliases[voiceId] || voiceId] || voiceProfiles.bella;
  const languageVoices = { "hi-IN":"Lekha", "ta-IN":"Vani", "te-IN":"Geeta", "kn-IN":"Soumya", "bn-IN":"Piya" };
  return { ...profile, systemVoice: languageVoices[language] || profile.systemVoice };
}

async function synthesizeWithElevenLabs(key, text, profile, rate, wav = false) {
  const normalizedRate = Math.max(40, Math.min(120, Number(rate) || 72));
  const speed = Math.max(0.7, Math.min(1.2, 0.75 + (normalizedRate - 40) / 160));
  const model = process.env.ELEVENLABS_MODEL || "eleven_multilingual_v2";
  const response = await fetch(`https://api.elevenlabs.io/v1/text-to-speech/${profile.elevenVoice}?output_format=${wav?'pcm_16000':'mp3_44100_128'}`, {
    method: "POST",
    headers: { "xi-api-key":key, "content-type":"application/json", accept:"audio/mpeg" },
    signal: AbortSignal.timeout(20000),
    body: JSON.stringify({
      text,
      model_id: model,
      voice_settings: { stability:0.58, similarity_boost:0.82, style:0.12, use_speaker_boost:true, speed }
    })
  });
  if (!response.ok) {
    let body;
    try { body = await response.json(); } catch {}
    const detail = body?.detail && typeof body.detail === 'object' ? body.detail : body;
    const code = String(detail?.code || detail?.status || '').replace(/[^a-z0-9_]/gi, '').slice(0, 64);
    const error = new Error(`ElevenLabs TTS HTTP ${response.status}${code ? ` (${code})` : ''}`);
    error.limitReached = isElevenLabsLimitError(response.status, body);
    throw error;
  }
  let audio=Buffer.from(await response.arrayBuffer());
  if(!audio.length)throw new Error('ElevenLabs returned empty audio');
  if(wav) {
    const header=Buffer.alloc(44);header.write('RIFF');header.writeUInt32LE(audio.length+36,4);header.write('WAVEfmt ',8);header.writeUInt32LE(16,16);header.writeUInt16LE(1,20);header.writeUInt16LE(1,22);header.writeUInt32LE(16000,24);header.writeUInt32LE(32000,28);header.writeUInt16LE(2,32);header.writeUInt16LE(16,34);header.write('data',36);header.writeUInt32LE(audio.length,40);audio=Buffer.concat([header,audio]);
  }
  return { audio, mime:wav?"audio/wav":"audio/mpeg", engine:"elevenlabs", voice:profile.label, model };
}

function pcmWave(pcm, sampleRate = 24000) {
  const header=Buffer.alloc(44);
  header.write('RIFF');header.writeUInt32LE(pcm.length+36,4);header.write('WAVEfmt ',8);
  header.writeUInt32LE(16,16);header.writeUInt16LE(1,20);header.writeUInt16LE(1,22);
  header.writeUInt32LE(sampleRate,24);header.writeUInt32LE(sampleRate*2,28);
  header.writeUInt16LE(2,32);header.writeUInt16LE(16,34);header.write('data',36);header.writeUInt32LE(pcm.length,40);
  return Buffer.concat([header,pcm]);
}

async function synthesizeWithGemini(text, profile, rate) {
  const key=process.env.GEMINI_API_KEY;
  if(!key)return null;
  const model=process.env.GEMINI_TTS_MODEL||'gemini-3.1-flash-tts-preview';
  const normalizedRate=Math.max(40,Math.min(120,Number(rate)||72));
  const pace=normalizedRate<62?'a little slowly and clearly':normalizedRate>88?'a little briskly but clearly':'at a calm, natural pace';
  const response=await fetch(`https://generativelanguage.googleapis.com/v1beta/models/${encodeURIComponent(model)}:generateContent`,{
    method:'POST',headers:{'x-goog-api-key':key,'content-type':'application/json'},signal:AbortSignal.timeout(25000),
    body:JSON.stringify({contents:[{parts:[{text:`Read this exactly, ${pace}: ${text}`}]}],generationConfig:{responseModalities:['AUDIO'],speechConfig:{voiceConfig:{prebuiltVoiceConfig:{voiceName:profile.geminiVoice}}}}})
  });
  const result=await response.json();
  if(!response.ok)throw new Error(result?.error?.message||`Gemini TTS returned ${response.status}`);
  const part=result?.candidates?.[0]?.content?.parts?.find(item=>item.inlineData?.data);
  if(!part?.inlineData?.data)throw new Error('Gemini TTS returned no audio');
  const mime=String(part.inlineData.mimeType||'');
  const rateMatch=/rate=(\d+)/.exec(mime),sampleRate=rateMatch?Number(rateMatch[1]):24000;
  const pcm=Buffer.from(part.inlineData.data,'base64');
  if(!pcm.length||pcm.length%2)throw new Error('Gemini TTS returned invalid PCM audio');
  return {audio:pcmWave(pcm,sampleRate),mime:'audio/wav',engine:'gemini-tts',voice:profile.label,model};
}

async function synthesizeWithNvidiaTts(text, language, profile) {
  const baseUrl = process.env.NVIDIA_TTS_URL?.replace(/\/$/, "");
  if (!baseUrl) return null;
  // Magpie uses en-US for all English text, while the macOS fallback can keep
  // a regional installed voice for non-English Indian languages.
  const nimLanguage = String(language || "en-US").startsWith("en") ? "en-US" : language;
  const form = new FormData();
  form.set("text", text); form.set("language", nimLanguage || "en-US");
  form.set("voice", profile.nimVoice); form.set("sample_rate_hz", "22050");
  const headers = process.env.NVIDIA_TTS_API_KEY ? { authorization: `Bearer ${process.env.NVIDIA_TTS_API_KEY}` } : {};
  const response = await fetch(`${baseUrl}/v1/audio/synthesize`, { method:"POST", headers, body:form });
  if (!response.ok) throw new Error(`NVIDIA TTS returned ${response.status}`);
  return Buffer.from(await response.arrayBuffer());
}

async function synthesizeNaturalVoice(text, voiceId, language, rate, wav = false) {
  const profile = resolveVoice(voiceId, language);
  const orderedKeys = elevenLabsKeys.orderedKeys();
  for (const [index, key] of orderedKeys.entries()) {
    try {
      return await synthesizeWithElevenLabs(key, text, profile, rate, wav);
    } catch (error) {
      if (error.limitReached) {
        try { await elevenLabsKeys.moveToLast(key); }
        catch { addEvent('storage_error', 'Could not save ElevenLabs key priority'); }
      }
      addEvent('tts_fallback', `ElevenLabs attempt ${index + 1} failed (${error.message})`);
    }
  }
  try {
    const geminiAudio=await synthesizeWithGemini(text,profile,rate);
    if(geminiAudio)return geminiAudio;
  } catch(error) {
    addEvent('tts_fallback',`ElevenLabs keys exhausted; Gemini TTS unavailable (${error.message})`);
  }
  const cloudAudio = await synthesizeWithNvidiaTts(text, language, profile);
  if (cloudAudio) return { audio: cloudAudio, mime:"audio/wav", engine:"nvidia-speech-nim", voice:profile.nimVoice };

  if (process.platform !== "darwin") throw new Error("No neural TTS endpoint configured. Set NVIDIA_TTS_URL.");
  const directory = await mkdtemp(join(tmpdir(), "buddy-voice-"));
  const aiffPath = join(directory, "speech.aiff");
  const wavPath = join(directory, "speech.wav");
  const normalizedRate = Math.max(40, Math.min(120, Number(rate) || 72));
  const wordsPerMinute = Math.round(135 + (normalizedRate - 40) * 0.875 + profile.rateOffset);
  try {
    await execFileAsync("/usr/bin/say", ["-v", profile.systemVoice, "-r", String(wordsPerMinute), "-o", aiffPath, text], { timeout:30000 });
    await execFileAsync("/usr/bin/afconvert", ["-f", "WAVE", "-d", "LEI16@22050", "-c", "1", aiffPath, wavPath], { timeout:30000 });
    return { audio:await readFile(wavPath), mime:"audio/wav", engine:"macos-natural", voice:profile.systemVoice };
  } finally {
    await rm(directory, { recursive:true, force:true });
  }
}

async function apiRoute(req, res, url) {
  if(url.pathname==='/api/transcribe'&&req.method==='POST') {
    if(!process.env.ELEVENLABS_API_KEY)return json(res,503,{error:'Set ELEVENLABS_API_KEY for microphone transcription'});
    const value=await bodyJson(req);
    const match=/^data:audio\/wav;base64,([A-Za-z0-9+/=]+)$/.exec(value.audio||'');
    if(!match)return json(res,400,{error:'Send a WAV utterance'});
    const wav=Buffer.from(match[1],'base64');
    if(wav.length<44 || wav.length>320044 || wav.toString('ascii',0,4)!=='RIFF' || wav.toString('ascii',8,16)!=='WAVEfmt ' || wav.readUInt32LE(16)!==16 || wav.readUInt16LE(20)!==1 || wav.readUInt16LE(22)!==1 || wav.readUInt32LE(24)!==16000 || wav.readUInt16LE(34)!==16 || wav.toString('ascii',36,40)!=='data' || wav.readUInt32LE(40)!==wav.length-44)return json(res,400,{error:'Send at most ten seconds of standard 16 kHz mono PCM WAV audio'});
    const form=new FormData();form.set('file',new Blob([wav],{type:'audio/wav'}),'utterance.wav');form.set('model_id','scribe_v2');form.set('tag_audio_events','false');
    const response=await fetch('https://api.elevenlabs.io/v1/speech-to-text',{method:'POST',headers:{'xi-api-key':process.env.ELEVENLABS_API_KEY},body:form,signal:AbortSignal.timeout(20000)});
    if(!response.ok)return json(res,502,{error:`Speech recognition unavailable (${response.status}). Use the text input until the service recovers.`});
    const result=await response.json();return json(res,200,{text:String(result.text||'').slice(0,500)});
  }
  if(url.pathname==='/api/prescription/scan'&&req.method==='POST') {
    const value=await bodyJson(req);
    let text=String(value.text||'').slice(0,12000),confidence=null;
    if(value.image) {
      const match=/^data:image\/(jpeg|png);base64,([A-Za-z0-9+/=]+)$/.exec(value.image);
      if(!match)return json(res,400,{error:'Use a JPEG or PNG prescription photo'});
      if(process.platform!=='darwin')return json(res,503,{error:'Photo reading needs the Mac companion server. Paste the prescription text to review it here.'});
      const directory=await mkdtemp(join(tmpdir(),'buddy-prescription-'));
      try{
        const path=join(directory,'prescription.'+match[1]);await writeFile(path,Buffer.from(match[2],'base64'),{mode:0o600});
        const executable=join(here,'build','prescription-ocr');
        if(!existsSync(executable)){await mkdir(join(here,'build'),{recursive:true});await execFileAsync('/usr/bin/swiftc',[join(here,'prescription-ocr.swift'),'-module-cache-path',join(here,'build','swift-cache'),'-o',executable],{timeout:120000});}
        const result=JSON.parse((await execFileAsync(executable,[path],{timeout:30000})).stdout);
        text=result.text;confidence=result.confidence;
      } finally {await rm(directory,{recursive:true,force:true});}
    }
    if(!text.trim())return json(res,400,{error:'No readable text found. Retake a clear photo or paste the text.'});
    return json(res,200,{...prescriptionDraft(text,localDate()),confidence,engine:value.image?'macos-vision-local':'reviewed-text'});
  }
  if(url.pathname==='/api/prescription/review'&&req.method==='POST') {
    const value=await bodyJson(req);
    try{return json(res,200,{reminders:reviewedReminders(value.items)});}
    catch(error){return json(res,400,{error:error.message});}
  }
  if (url.pathname === "/api/tts/voices" && req.method === "GET") {
    return json(res, 200, { engine:elevenLabsKeys.size ? "elevenlabs" : process.env.GEMINI_API_KEY ? "gemini-tts" : process.env.NVIDIA_TTS_URL ? "nvidia-speech-nim" : process.platform === "darwin" ? "macos-natural" : "browser-fallback", fallback:elevenLabsKeys.size&&process.env.GEMINI_API_KEY?'gemini-tts':null, voices:Object.entries(voiceProfiles).map(([id, value]) => ({ id, label:value.label })) });
  }
  if (url.pathname === "/api/tts" && req.method === "POST") {
    const value = await bodyJson(req);
    const text = String(value.text||'').replace(/[\t\r\n]+/g,' ').slice(0, 600);
    if (!text) return json(res, 400, { error:"Text is required" });
    const result = await synthesizeNaturalVoice(text, value.voiceId, value.language, value.rate, value.format==='wav');
    res.writeHead(200, { "content-type":result.mime, "content-length":result.audio.length, "cache-control":"no-store", "access-control-allow-origin":"*", "access-control-expose-headers":"x-buddy-voice,x-buddy-tts-engine,x-buddy-tts-model", "x-buddy-voice":result.voice, "x-buddy-tts-engine":result.engine, "x-buddy-tts-model":result.model || "" });
    return res.end(result.audio);
  }
  return json(res, 404, { error: "API route not found" });
}

const mime = { ".html":"text/html; charset=utf-8", ".js":"text/javascript; charset=utf-8", ".css":"text/css; charset=utf-8", ".json":"application/json; charset=utf-8", ".svg":"image/svg+xml", ".png":"image/png" };

async function staticFile(req, res, url) {
  let base = appRoot;
  let requestPath = url.pathname;
  if (requestPath === "/device" || requestPath.startsWith("/device/")) return json(res, 404, { error: "Hardware-only Buddy does not provide a virtual device" });
  if (requestPath === "/") requestPath = '/portal.html';
  if (requestPath === '/settings' || requestPath === '/settings/') requestPath='/index.html';
  const file = normalize(join(base, requestPath));
  if (!file.startsWith(base)) return json(res, 403, { error: "Forbidden" });
  try {
    const info = await stat(file);
    if (!info.isFile()) throw new Error("not file");
    res.writeHead(200, { "content-type": mime[extname(file)] || "application/octet-stream", "cache-control": "no-cache" });
    createReadStream(file).pipe(res);
  } catch { json(res, 404, { error: "File not found" }); }
}

await loadLocalEnv();
const storageMode = process.env.BUDDY_STORAGE || 'local';
if(!['local','supabase'].includes(storageMode))throw new Error('BUDDY_STORAGE must be local or supabase.');
const cloudState = storageMode === 'supabase' ? new SupabaseState({
  url:process.env.SUPABASE_URL,
  secretKey:process.env.SUPABASE_SECRET_KEY
}) : null;
function publicDeviceUrl() {
  const value = process.env.BUDDY_PUBLIC_URL || process.env.RENDER_EXTERNAL_URL;
  if (!value) return null;
  try {
    const url = new URL(value);
    return url.protocol === 'https:' && !url.username && !url.password &&
      url.pathname === '/' && !url.search && !url.hash ? url.origin : null;
  } catch { return null; }
}
elevenLabsKeys = await new ElevenLabsKeyRing(
  configuredElevenLabsKeys(process.env.ELEVENLABS_API_KEY, process.env.ELEVENLABS_API_KEYS),
  join(stateDirectory, 'elevenlabs-key-order.json'),
  cloudState?.forKey('elevenlabs-key-order')).load();
nvidiaKeys = await new ElevenLabsKeyRing(
  nvidiaChatKeys(process.env.NVIDIA_CHAT_API_KEYS),
  join(stateDirectory, 'nvidia-chat-key-order.json'),
  cloudState?.forKey('nvidia-chat-key-order')).load();
await loadSettings();
careHub=await new CareHub({directory:stateDirectory,defaults:settings,services:{assistant:callConversation,publicDeviceUrl:publicDeviceUrl()},stateStore:cloudState?.forKey('care-hub')}).init();

const handler = async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || `localhost:${port}`}`);
  try {
    if(await careHub.handle(req,res,url,{json,bodyJson,legacy:apiRoute}))return;
    if (url.pathname.startsWith("/api/")) {
      return json(res,401,{error:'Open the Buddy app and sign in to your care circle.'});
    }
    else await staticFile(req, res, url);
  } catch (error) { json(res, 500, { error: error.message || "Unexpected server error" }); }
};
const server=process.env.BUDDY_TLS_CERT&&process.env.BUDDY_TLS_KEY?createSecureServer({cert:await readFile(process.env.BUDDY_TLS_CERT),key:await readFile(process.env.BUDDY_TLS_KEY)},handler):createServer(handler);

server.listen(port, process.env.BUDDY_HOST || "127.0.0.1", () => {
  console.log(`Buddy Care:       http://localhost:${server.address().port}/`);
  console.log(`Buddy storage:    ${storageMode}`);
  console.log(`ElevenLabs TTS:    ${elevenLabsKeys.size ? `configured (primary, ${elevenLabsKeys.size} keys)` : 'disabled (set ELEVENLABS_API_KEY)'}`);
  console.log(`Gemini TTS:        ${process.env.GEMINI_API_KEY ? "configured (fallback)" : "disabled (set GEMINI_API_KEY)"}`);
  console.log(`NVIDIA assistant: ${nvidiaKeys.size ? `configured (${process.env.NVIDIA_CHAT_MODEL || DEFAULT_NVIDIA_CHAT_MODEL}, ${nvidiaKeys.size} keys)` : 'disabled (set NVIDIA_CHAT_API_KEYS)'}`);
});

function shutdown() { careHub.close();server.close(() => process.exit(0));setTimeout(() => process.exit(0), 500).unref(); }
process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);
