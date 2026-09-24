import test,{before,after} from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp,rm,readFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {setTimeout as pause} from 'node:timers/promises';
let server,base,directory,owner,supervisor,stranger,circle,device,settings;
async function start(){let log='';server=spawn(process.execPath,['care-hub/server.mjs'],{env:{...process.env,PORT:'0',BUDDY_HOST:'127.0.0.1',BUDDY_STATE_DIR:directory,BUDDY_SKIP_ENV:'1',NVIDIA_CHAT_API_KEYS:'',ELEVENLABS_API_KEY:'',NVIDIA_TTS_URL:''},stdio:['ignore','pipe','pipe']});server.stdout.on('data',b=>log+=b);server.stderr.on('data',b=>log+=b);for(let i=0;i<100;i++){const m=/Buddy Care:\s+http:\/\/localhost:(\d+)/.exec(log);if(m){base='http://127.0.0.1:'+m[1];return;}await pause(50);}throw Error(log);}
async function stop(){if(server?.exitCode===null){const done=new Promise(r=>server.once('exit',r));server.kill();await done;}}
async function api(path,account,body,method){const r=await fetch(base+'/api/v2'+path,{method:method||(body?'POST':'GET'),headers:{'content-type':'application/json',...(account?{authorization:'Bearer '+(account.token||account.deviceToken)}:{})},body:body?JSON.stringify(body):undefined});return {status:r.status,body:await r.json()};}
async function register(name,role){const r=await api('/auth/register',null,{name,role,email:name.toLowerCase()+'@example.test',password:'Unique-demo-password-42'});assert.equal(r.status,201);return r.body;}
const cp=()=>'/circles/'+circle;
before(async()=>{directory=await mkdtemp(join(tmpdir(),'buddy-hub-test-'));await start();owner=await register('Anita','user');supervisor=await register('Priya','supervisor');stranger=await register('Stranger','user');circle=(await api('/me',owner)).body.circles[0].id;});
after(async()=>{await stop();if(directory)await rm(directory,{recursive:true,force:true});});
test('accounts are isolated and legacy care endpoints are not public',async()=>{
 assert.equal((await fetch(base+'/api/settings')).status,401);
 assert.equal((await api(cp(),null)).status,401);assert.equal((await api(cp(),stranger)).status,403);
 assert.equal((await api(cp(),supervisor)).status,403);
 const stored=await readFile(join(directory,'care-hub.json'),'utf8');assert.ok(!stored.includes(owner.token));assert.ok(!stored.includes('Unique-demo-password-42'));assert.ok(stored.includes('passwordHash'));
});
test('website chatbot is private to the circle and reports bad input or provider outage',async()=>{
 const route=cp()+'/bridge/api/assistant';
 assert.equal((await api(route,stranger,{message:'Hello'})).status,403);
 assert.equal((await api(route,owner,{message:'   '})).status,400);
 assert.equal((await api(route,owner,{message:'Hello',history:{bad:true}})).status,400);
 assert.equal((await api(route,owner,{message:'Hello'})).status,503);
});
test('one-use invites grant scoped, revocable access',async()=>{
 const invitation=(await api(cp()+'/invite',owner,{canEdit:false,canCamera:true})).body;
 assert.equal((await api('/join',supervisor,{code:invitation.code})).status,200);
 assert.equal((await api('/join',supervisor,{code:invitation.code})).status,400);
 settings=(await api(cp()+'/bridge/api/settings',owner)).body;
 assert.equal((await api(cp()+'/bridge/api/settings',supervisor,settings,'PUT')).status,403);
 assert.equal((await api(cp()+'/invite',supervisor,{})).status,403);
 assert.equal((await api(cp()+'/members',owner,{userId:supervisor.user.id,canEdit:true})).status,200);
});
test('pairing uses short-lived codes and a private device credential',async()=>{
 device=(await api('/device/enroll',null,{name:'Physical test adapter',capabilities:{camera:true,audio:true}})).body;
 assert.equal((await api(cp()+'/devices/claim',supervisor,{code:device.pairCode})).status,403);
 assert.equal((await api(cp()+'/devices/claim',owner,{code:device.pairCode})).status,200);
 assert.equal((await api(cp()+'/devices/claim',owner,{code:device.pairCode})).status,400);
 const sync=(await api('/device/sync',device,{appliedRevision:0,status:{mode:'idle'}})).body;assert.equal(sync.paired,true);assert.equal(sync.revision,1);assert.equal(sync.settings.person.name,'Anita');
 assert.equal((await api('/device/sync',owner,{})).status,401);
 const stored=await readFile(join(directory,'care-hub.json'),'utf8');assert.ok(!stored.includes(device.deviceToken));assert.ok(!stored.includes(device.pairCode));
});
test('versioned settings remain pending until device acknowledges successful application',async()=>{
 settings.device.callName='Momo';settings.display.eyeColor=32724;let r=await api(cp()+'/bridge/api/settings',supervisor,settings,'PUT');assert.equal(r.status,200);settings=r.body;
 assert.equal((await api(cp()+'/bridge/api/settings',owner,{...settings,_revision:1},'PUT')).status,409);
 assert.equal((await api(cp(),owner)).body.devices[0].appliedRevision,0);
 const sync=(await api('/device/sync',device,{appliedRevision:0})).body;assert.equal(sync.settings.device.callName,'Momo');assert.equal(sync.settings.display.eyeColor,32724);
 await api('/device/sync',device,{appliedRevision:2});assert.equal((await api(cp(),owner)).body.devices[0].appliedRevision,2);
 const status=(await api(cp()+'/bridge/api/status',owner)).body;assert.equal(status.online,true);assert.equal(status.paired,true);assert.equal(status.appliedRevision,2);
 assert.equal((await api('/device/sync',device,{appliedRevision:2})).body.settings,null);
});
test('durable commands and device events are acknowledged and deduplicated',async()=>{
 await api(cp()+'/messages',supervisor,{message:'Hello from Priya'});
 let sync=(await api('/device/sync',device,{appliedRevision:2})).body;const say=sync.commands.find(c=>c.type==='say');assert.ok(say);
 const event={eventId:'event-test-1',type:'sos_escalated',reminderId:'sos',message:'Help requested'};
 await api('/device/sync',device,{appliedRevision:2,ack:[say.id],events:[event,event]});
 sync=(await api('/device/sync',device,{appliedRevision:2,events:[event]})).body;assert.ok(!sync.commands.some(c=>c.id===say.id));
 const v=(await api(cp(),owner)).body;assert.equal(v.journal.filter(e=>e.id==='event-test-1').length,1);assert.equal(v.alerts.filter(a=>a.ref==='event-test-1').length,1);
});
test('care team can claim tasks, answer check-ins and coordinate alerts',async()=>{
 await api(cp()+'/tasks',supervisor,{title:'Collect the prescription'});let v=(await api(cp(),owner)).body;const task=v.tasks[0];
 assert.equal((await api(cp()+'/tasks',supervisor,{id:task.id,claim:true})).status,200);
 assert.equal((await api(cp()+'/tasks',owner,{id:task.id,claim:true})).status,409);
 assert.equal((await api(cp()+'/tasks',supervisor,{id:task.id,done:true})).status,200);
 await api(cp()+'/checkins',supervisor,{minutes:1});assert.equal((await api(cp()+'/checkins',supervisor,{answer:'okay'})).status,403);
 await api(cp()+'/checkins',owner,{answer:'okay'});v=(await api(cp(),owner)).body;assert.equal(v.checkins[0].status,'answered');
 const alert=v.alerts[0];assert.equal((await api(cp()+'/alerts',supervisor,{id:alert.id})).status,200);assert.equal((await api(cp()+'/alerts',owner,{id:alert.id})).status,409);
 await api(cp()+'/alerts',supervisor,{id:alert.id,resolve:true});
});
test('camera requires acceptance, participant auth and user permission; media is discarded on end',async()=>{
 let r=await api(cp()+'/calls',supervisor,{to:device.deviceId,kind:'camera'});assert.equal(r.status,201);const call=r.body.call;
 assert.equal((await api('/calls/'+call.id+'/media',supervisor,{})).status,409);
 assert.equal((await api('/calls/'+call.id+'/accept',supervisor,{})).status,403);
 assert.equal((await api('/calls/'+call.id,stranger)).status,403);
 await api('/device/sync',device,{appliedRevision:2,events:[{eventId:'camera-answer',type:'call_control',reminderId:'accept'}]});
 r=await api('/device/media',device,{callId:call.id,jpeg:Buffer.from([255,216,255,217]).toString('base64')});assert.equal(r.status,200);
 r=await api('/calls/'+call.id+'/media',supervisor,{});assert.ok(r.body.jpeg);assert.equal(r.body.frameSeq,1);
 assert.equal((await api('/device/media',device,{callId:call.id,audio:'AAAA'})).status,400);
 await api(cp()+'/privacy',owner,{cameraRequests:false},'PUT');assert.equal((await api('/calls/'+call.id+'/media',supervisor,{})).status,409);
 assert.equal((await api(cp()+'/calls',supervisor,{to:device.deviceId,kind:'camera'})).status,403);
 await api(cp()+'/privacy',owner,{cameraRequests:true},'PUT');
});
test('device-to-app calling carries PCM both ways and handles explicit hang-up',async()=>{
 await api('/device/sync',device,{appliedRevision:2,events:[{eventId:'outbound-1',type:'call_control',reminderId:'start',message:'priya'}]});
 let v=(await api(cp(),supervisor)).body;const call=v.calls.find(c=>c.status==='ringing');assert.equal(call.from,device.deviceId);
 assert.equal((await api('/calls/'+call.id+'/accept',supervisor,{})).status,200);
 const audio=Buffer.from([0,0,255,127]).toString('base64');await api('/device/media',device,{callId:call.id,audio});
 const r=await api('/calls/'+call.id+'/media',supervisor,{audio});assert.equal(r.body.audio[0].audio,audio);
 assert.equal((await api('/device/media',device,{callId:call.id})).body.audio[0].audio,audio);
 assert.equal((await api('/calls/'+call.id+'/media',supervisor,{after:1})).body.audio.length,0);
 await api('/device/sync',device,{appliedRevision:2,events:[{eventId:'outbound-end',type:'call_control',reminderId:'end'}]});
 assert.equal((await api('/calls/'+call.id,supervisor)).body.call.status,'ended');
});
test('app-to-app WebRTC signaling is scoped to accepted participants',async()=>{
 const call=(await api(cp()+'/calls',supervisor,{to:owner.user.id,kind:'video'})).body.call;
 assert.equal((await api('/calls/'+call.id+'/signal',supervisor,{type:'offer',data:{type:'offer',sdp:'test'}})).status,409);
 await api('/calls/'+call.id+'/accept',owner,{});
 assert.equal((await api('/calls/'+call.id+'/signal',supervisor,{type:'offer',data:{type:'offer',sdp:'test'}})).status,200);
 assert.equal((await api('/calls/'+call.id+'/signal',owner)).body.signals.length,1);
 assert.equal((await api('/calls/'+call.id+'/signal',stranger)).status,403);
 await api(cp()+'/members',owner,{userId:supervisor.user.id,canCall:false});
 assert.equal((await api('/calls/'+call.id,owner)).body.call.status,'ended');
 await api(cp()+'/members',owner,{userId:supervisor.user.id,canCall:true});
});
test('only physical hardware can pair and provide device status',async()=>{
 const status=(await api(cp()+'/bridge/api/status',owner)).body;
 assert.equal(status.paired,true);assert.equal(status.firmwareVersion,'ESP32-S3');
 assert.equal((await api(cp()+'/devices/simulator',owner,{})).status,404);
 assert.equal((await api(cp()+'/devices/simulator/connection',owner,{online:true})).status,404);
 assert.equal((await api(cp()+'/bridge/api/voice',owner,{text:'Hey Buddy'})).status,404);
 const cal=(await api(cp()+'/bridge/api/calendar?month=2026-09',owner)).body;
 assert.equal(cal.days.length,30);assert.equal(cal.source,'Care Hub schedule');
});
test('accounts, settings and pairing survive restart without resurrecting calls',async()=>{
 await stop();await start();const v=(await api(cp(),owner)).body;assert.equal(v.settings.device.callName,'Momo');assert.equal(v.members.length,2);assert.ok(v.devices.some(d=>d.id===device.deviceId));assert.ok(v.calls.every(c=>!['accepted','ringing'].includes(c.status)));
 assert.equal((await api('/device/sync',device,{appliedRevision:2})).body.paired,true);
});
