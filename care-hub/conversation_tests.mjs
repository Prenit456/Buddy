import test from 'node:test';
import assert from 'node:assert/strict';
import {openRouterKeys, openRouterConversation} from './conversation-provider.mjs';

test('OpenRouter key order is stable and repeated keys are not retried', () => {
  assert.deepEqual(openRouterKeys(' first, second,first, ,third '), ['first','second','third']);
});

test('conversation retries rate limits and empty replies using the next key', async () => {
  const attempted=[];
  const replies=[
    {ok:false,status:429},
    {ok:true,json:async()=>({choices:[{message:{content:''}}]})},
    {ok:true,json:async()=>({model:'free/test-model',choices:[{message:{content:'  Hello, Friend.  '}}]})}
  ];
  const fetchImpl=async (url,options) => {
    assert.equal(url,'https://openrouter.ai/api/v1/chat/completions');
    attempted.push(options.headers.authorization);
    const request=JSON.parse(options.body);
    assert.equal(request.model,'openrouter/free');
    assert.equal(request.messages.at(-1).content,'How are you?');
    assert.match(request.messages[0].content,/Never diagnose/);
    return replies.shift();
  };
  const result=await openRouterConversation({message:'How are you?',careSettings:{person:{name:'Friend',language:'English'}},
    keys:'key-one,key-two,key-three',fetchImpl});
  assert.deepEqual(attempted,['Bearer key-one','Bearer key-two','Bearer key-three']);
  assert.deepEqual(result,{answer:'Hello, Friend.',model:'free/test-model'});
});

test('conversation returns a credential-free error when every key fails', async () => {
  await assert.rejects(openRouterConversation({message:'Hello',keys:'private-one,private-two',
    fetchImpl:async(_url,options) => {
      if(options.headers.authorization==='Bearer private-one')throw Error('network error');
      return {ok:false,status:401};
    }}),error => {
      assert.equal(error.message,'OpenRouter conversation is unavailable with the configured keys');
      return true;
    });
});

test('conversation rejects empty, oversized and malformed history before calling a provider', async () => {
  const unavailable = async () => { throw Error('Provider should not be called'); };
  for (const message of ['', ' '.repeat(5), 'a'.repeat(1001)])
    await assert.rejects(openRouterConversation({message,keys:'test-key',fetchImpl:unavailable}),
      error => error.status === 400);
  await assert.rejects(openRouterConversation({message:'Hello',history:{role:'user'},keys:'test-key',fetchImpl:unavailable}),
    error => error.status === 400);
});
