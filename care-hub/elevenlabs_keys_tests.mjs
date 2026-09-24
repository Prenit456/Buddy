import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp, readFile, rm, stat} from 'node:fs/promises';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {configuredElevenLabsKeys, ElevenLabsKeyRing, isElevenLabsLimitError} from './elevenlabs-keys.mjs';

test('ElevenLabs keys preserve primary order and skip duplicates', () => {
  assert.deepEqual(configuredElevenLabsKeys('primary', ' backup-one,primary;backup-two '),
    ['primary', 'backup-one', 'backup-two']);
});

test('quota and rate limits are distinguished from invalid credentials', () => {
  assert.equal(isElevenLabsLimitError(401, {detail:{status:'quota_exceeded'}}), true);
  assert.equal(isElevenLabsLimitError(402, {detail:{code:'insufficient_credits'}}), true);
  assert.equal(isElevenLabsLimitError(429, {detail:{code:'rate_limit_exceeded'}}), true);
  assert.equal(isElevenLabsLimitError(401, {detail:{status:'invalid_api_key'}}), false);
});

test('a limited key moves to the end for later requests and after restart', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'buddy-elevenlabs-keys-'));
  t.after(() => rm(directory, {recursive:true, force:true}));
  const statePath = join(directory, 'priority.json');
  const keys = ['secret-primary', 'secret-backup-one', 'secret-backup-two'];
  const ring = await new ElevenLabsKeyRing(keys, statePath).load();
  assert.deepEqual(ring.orderedKeys(), keys);
  await ring.moveToLast(keys[0]);
  assert.deepEqual(ring.orderedKeys(), [keys[1], keys[2], keys[0]]);
  await ring.moveToLast(keys[1]);
  assert.deepEqual(ring.orderedKeys(), [keys[2], keys[0], keys[1]]);
  const saved = await readFile(statePath, 'utf8');
  for (const key of keys) assert.equal(saved.includes(key), false);
  assert.equal((await stat(statePath)).mode & 0o777, 0o600);
  const restarted = await new ElevenLabsKeyRing(keys, statePath).load();
  assert.deepEqual(restarted.orderedKeys(), [keys[2], keys[0], keys[1]]);
});
