import test from 'node:test';
import assert from 'node:assert/strict';
import {nvidiaChatKeys, nvidiaConversation, DEFAULT_NVIDIA_CHAT_MODEL} from './conversation-provider.mjs';

test('NVIDIA key order is stable and repeated keys are not retried', () => {
  assert.deepEqual(nvidiaChatKeys(' first, second\nfirst; third '), ['first','second','third']);
});

test('NVIDIA conversation retries rate limits and empty replies using the next key', async () => {
  const attempted=[];
  const rejected=[];
  const replies=[
    {ok:false,status:429},
    {ok:true,json:async()=>({choices:[{message:{content:''}}]})},
    {ok:true,json:async()=>({model:DEFAULT_NVIDIA_CHAT_MODEL,choices:[{message:{content:'  Hello, Friend.  '}}]})}
  ];
  const fetchImpl=async (url,options) => {
    assert.equal(url,'https://integrate.api.nvidia.com/v1/chat/completions');
    attempted.push(options.headers.authorization);
    const request=JSON.parse(options.body);
    assert.equal(request.model,DEFAULT_NVIDIA_CHAT_MODEL);
    assert.equal(request.reasoning_effort,'none');
    assert.equal(request.temperature,0.95);
    assert.equal(request.top_p,1);
    assert.equal(request.messages.at(-1).content,'How are you?');
    assert.match(request.messages[0].content,/Never diagnose/);
    return replies.shift();
  };
  const result=await nvidiaConversation({message:'How are you?',careSettings:{person:{name:'Friend',language:'English'}},
    keys:'key-one,key-two,key-three',fetchImpl,onKeyRejected:async(key,status)=>rejected.push([key,status])});
  assert.deepEqual(attempted,['Bearer key-one','Bearer key-two','Bearer key-three']);
  assert.deepEqual(rejected,[['key-one',429]]);
  assert.deepEqual(result,{answer:'Hello, Friend.',model:DEFAULT_NVIDIA_CHAT_MODEL});
});

test('NVIDIA conversation returns a credential-free error when every key fails', async () => {
  await assert.rejects(nvidiaConversation({message:'Hello',keys:'private-one,private-two',
    fetchImpl:async(_url,options) => {
      if(options.headers.authorization==='Bearer private-one')throw Error('network error');
      return {ok:false,status:401};
    }}),error => {
      assert.equal(error.message,'NVIDIA chat is unavailable with the configured keys (provider HTTP 401)');
      assert.ok(!error.message.includes('private-'));
      return true;
    });
});

test('conversation rejects empty, oversized and malformed history before calling a provider', async () => {
  const unavailable = async () => { throw Error('Provider should not be called'); };
  for (const message of ['', ' '.repeat(5), 'a'.repeat(1001)])
    await assert.rejects(nvidiaConversation({message,keys:'test-key',fetchImpl:unavailable}),
      error => error.status === 400);
  await assert.rejects(nvidiaConversation({message:'Hello',history:{role:'user'},keys:'test-key',fetchImpl:unavailable}),
    error => error.status === 400);
});

test('NVIDIA conversation normalizes malformed history and stops on retired models', async () => {
  const calls=[];
  await assert.rejects(nvidiaConversation({message:'Hello',keys:'one,two',
    history:[{role:'assistant',content:'orphan'},{role:'user',content:'first'},
      {role:'user',content:'second'},{role:'assistant',content:'answer'},{role:'user',content:'unfinished'}],
    fetchImpl:async(_url,options) => {
      calls.push(JSON.parse(options.body).messages);
      return {ok:false,status:410};
    }}),error => error.status === 503 && /HTTP 410/.test(error.message));
  assert.equal(calls.length,1);
  assert.deepEqual(calls[0].slice(1).map(item=>item.role),['user','assistant','user']);
  assert.equal(calls[0][1].content,'second');
});

test('NVIDIA queued responses are polled with the same key', async () => {
  const id='12345678-1234-1234-1234-123456789abc';
  const urls=[];
  const result=await nvidiaConversation({message:'Hello',keys:'one',fetchImpl:async(url,options)=>{
    urls.push(url);
    assert.equal(options.headers.authorization,'Bearer one');
    if (urls.length===1)return {status:202,headers:{get:()=>id}};
    return {ok:true,status:200,json:async()=>({choices:[{message:{content:'Hello.'}}]})};
  }});
  assert.equal(urls[1],`https://api.nvcf.nvidia.com/v2/nvcf/pexec/status/${id}`);
  assert.equal(result.answer,'Hello.');
});
