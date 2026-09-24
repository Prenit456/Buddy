import {createHash} from 'node:crypto';
import {mkdir, readFile, rename, writeFile} from 'node:fs/promises';
import {dirname} from 'node:path';

export function configuredElevenLabsKeys(primary = '', backups = '') {
  return [...new Set([primary, ...String(backups).split(/[\s,;]+/)]
    .map(key => String(key || '').trim()).filter(Boolean))];
}

export function isElevenLabsLimitError(status, body) {
  const detail = body?.detail && typeof body.detail === 'object' ? body.detail : body;
  const code = String(detail?.code || detail?.status || '').toLowerCase();
  return status === 402 || status === 429 ||
    ['quota_exceeded', 'insufficient_credits', 'payment_required',
      'rate_limit_exceeded', 'concurrent_limit_exceeded',
      'too_many_concurrent_requests'].includes(code);
}

const keyId = key => createHash('sha256').update(key).digest('hex');

// Persist only fingerprints and priority, never the secret keys themselves.
export class ElevenLabsKeyRing {
  constructor(keys, statePath, stateStore = null) {
    this.configured = [...new Set(keys)].filter(Boolean);
    this.statePath = statePath;
    this.stateStore = stateStore;
    this.demoted = [];
    this.saveQueue = Promise.resolve();
  }

  async load() {
    try {
      const saved = this.stateStore ? await this.stateStore.load() : JSON.parse(await readFile(this.statePath, 'utf8'));
      const known = new Set(this.configured.map(keyId));
      if (Array.isArray(saved?.demoted))
        this.demoted = [...new Set(saved.demoted.filter(id => known.has(id)))];
    } catch (error) {
      if (error.code !== 'ENOENT' && !(error instanceof SyntaxError)) throw error;
    }
    return this;
  }

  get size() { return this.configured.length; }

  orderedKeys() {
    const byId = new Map(this.configured.map(key => [keyId(key), key]));
    const limited = new Set(this.demoted);
    return [
      ...this.configured.filter(key => !limited.has(keyId(key))),
      ...this.demoted.map(id => byId.get(id)).filter(Boolean)
    ];
  }

  async moveToLast(key) {
    if (!this.configured.includes(key)) return;
    const id = keyId(key);
    this.demoted = this.demoted.filter(value => value !== id);
    this.demoted.push(id);
    const saved = {demoted:[...this.demoted]};
    this.saveQueue = this.saveQueue.catch(() => {}).then(async () => {
      if (this.stateStore) { await this.stateStore.save(saved); return; }
      await mkdir(dirname(this.statePath), {recursive:true});
      const temporary = this.statePath + '.tmp';
      await writeFile(temporary, JSON.stringify(saved) + '\n', {mode:0o600});
      await rename(temporary, this.statePath);
    });
    await this.saveQueue;
  }
}
