import test from 'node:test';
import assert from 'node:assert/strict';
import { SupabaseState } from './supabase-state.mjs';
import { CareHub } from './care-hub.mjs';
import { ElevenLabsKeyRing } from './elevenlabs-keys.mjs';

function fakeSupabase() {
  const rows = new Map();
  const calls = [];
  const fetchImpl = async (url, options) => {
    calls.push({url:String(url), options});
    assert.equal(options.headers.apikey, 'sb_secret_test-only');
    assert.equal(options.headers.authorization, undefined);
    const id = options.method === 'GET' ? new URL(url).searchParams.get('id').slice(3) : JSON.parse(options.body).id;
    if (options.method === 'GET')
      return new Response(JSON.stringify(rows.has(id) ? [{value:rows.get(id)}] : []), {status:200});
    rows.set(id, JSON.parse(options.body).value);
    return new Response(null, {status:204});
  };
  const state = new SupabaseState({url:'https://buddy-test.supabase.co',secretKey:'sb_secret_test-only',fetchImpl});
  return {state,rows,calls};
}

test('Supabase state keeps Buddy and key-order data separate and server-only', async () => {
  const {state,rows,calls}=fakeSupabase();
  const accountState=state.forKey('care-hub'),keyState=state.forKey('elevenlabs-key-order');
  assert.equal(await accountState.load(),null);
  await accountState.save({users:[{id:'fictional'}]});
  await keyState.save({demoted:['fingerprint']});
  assert.deepEqual(await accountState.load(),{users:[{id:'fictional'}]});
  assert.deepEqual(await keyState.load(),{demoted:['fingerprint']});
  assert.equal(rows.size,2);
  assert.ok(calls.every(call=>new URL(call.url).hostname==='buddy-test.supabase.co'));
  assert.ok(calls.some(call=>call.options.headers.prefer==='resolution=merge-duplicates,return=minimal'));
});

test('Supabase configuration and write failures fail closed', async () => {
  assert.throws(()=>new SupabaseState({url:'http://example.com',secretKey:'sb_secret_test'}),/HTTPS origin/);
  assert.throws(()=>new SupabaseState({url:'https://example.com',secretKey:'sb_publishable_test'}),/server-only/);
  const state=new SupabaseState({url:'https://buddy-test.supabase.co',secretKey:'sb_secret_test-only',fetchImpl:async()=>new Response('private detail',{status:401})});
  await assert.rejects(state.forKey('care-hub').save({}),error=>error.message.includes('HTTP 401')&&!error.message.includes('private detail'));
});

test('Care Hub and ElevenLabs priority survive a server restart through Supabase', async () => {
  const {state}=fakeSupabase();
  const options={directory:'/ignored-in-cloud-mode',defaults:{},services:{},stateStore:state.forKey('care-hub')};
  const first=await new CareHub(options).init();
  first.db.users.push({id:'fictional-user'});
  await first.commit();first.close();
  const second=await new CareHub(options).init();
  assert.deepEqual(second.db.users,[{id:'fictional-user'}]);second.close();
  const keyStore=state.forKey('elevenlabs-key-order');
  const keys=['first-test-key','second-test-key'];
  const ring=await new ElevenLabsKeyRing(keys,'/ignored-in-cloud-mode',keyStore).load();
  await ring.moveToLast(keys[0]);
  const reloaded=await new ElevenLabsKeyRing(keys,'/ignored-in-cloud-mode',keyStore).load();
  assert.deepEqual(reloaded.orderedKeys(),[keys[1],keys[0]]);
});
