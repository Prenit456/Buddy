import {setTimeout as pause} from 'node:timers/promises';

export const DEFAULT_NVIDIA_CHAT_MODEL = 'meta/muse-glimmer-30b';
const CHAT_URL = 'https://integrate.api.nvidia.com/v1/chat/completions';
const STATUS_URL = 'https://api.nvcf.nvidia.com/v2/nvcf/pexec/status/';
const REQUEST_ID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
const sanitize = value => String(value ?? '').replace(/[\t\r\n]+/g, ' ').slice(0, 240);

// Render's value can be comma-separated or entered on separate lines.
export function nvidiaChatKeys(value) {
  return [...new Set(String(value || '').split(/[\s,;]+/).map(key => key.trim()).filter(Boolean))];
}

function conversationMessages(message, history, careSettings) {
  const person = careSettings?.person || {};
  const system = `You are Buddy, a calm voice companion for ${sanitize(person.name || 'Friend')}, an older adult. Reply in ${sanitize(person.language || 'English')}. Use plain, respectful language and no more than 55 words. Never diagnose, prescribe, or change medication. For urgent symptoms, falls, danger, or requests for emergency help, clearly advise contacting local emergency services and a trusted person. Do not pretend to be human or replace family. You cannot see today's live calendar, medicine schedule, camera, or device status; direct questions about exact reminders to the Calendar instead of guessing.`;
  // NVIDIA's chat API requires alternating user and assistant history.
  const recent = [];
  for (const item of history.slice(-6)) {
    if (!item || !['user', 'assistant'].includes(item.role) || typeof item.content !== 'string') continue;
    if (!recent.length && item.role === 'assistant') continue;
    const entry = {role:item.role, content:sanitize(item.content)};
    if (recent.at(-1)?.role === entry.role) recent[recent.length - 1] = entry;
    else recent.push(entry);
  }
  if (recent.at(-1)?.role === 'user') recent.pop();
  return [{role:'system',content:system}, ...recent, {role:'user',content:sanitize(message)}];
}

async function pendingResult(response, key, fetchImpl, attemptDeadline) {
  if (response.status !== 202) return response;
  let requestId = response.headers?.get?.('nvcf-reqid');
  if (!requestId) {
    try {
      const body = await response.json();
      requestId = body?.requestId || body?.request_id;
    } catch { /* A missing request ID lets the next key try. */ }
  }
  if (!REQUEST_ID.test(String(requestId || ''))) return response;
  while (response.status === 202 && Date.now() + 750 < attemptDeadline) {
    await pause(500);
    response = await fetchImpl(STATUS_URL + requestId, {
      method:'GET',
      headers:{authorization:`Bearer ${key}`,accept:'application/json'},
      signal:AbortSignal.timeout(Math.max(1,attemptDeadline - Date.now()))
    });
  }
  return response;
}

export async function nvidiaConversation({message, history = [], careSettings, keys,
    model = DEFAULT_NVIDIA_CHAT_MODEL, fetchImpl = fetch, timeoutMs = 12000,
    totalTimeoutMs = 45000, onKeyRejected = async () => {}}) {
  if (typeof message !== 'string' || !message.trim() || message.length > 1000)
    throw Object.assign(new Error('Enter a message of up to 1000 characters.'), {status:400});
  if (!Array.isArray(history))
    throw Object.assign(new Error('Conversation history must be a list.'), {status:400});
  const availableKeys = Array.isArray(keys) ? [...new Set(keys.filter(Boolean))] : nvidiaChatKeys(keys);
  if (!availableKeys.length)
    throw Object.assign(new Error('NVIDIA chat keys are not configured'), {status:503});

  const messages = conversationMessages(message, history, careSettings);
  const deadline = Date.now() + totalTimeoutMs;
  let lastStatus;
  for (const key of availableKeys) {
    if (Date.now() >= deadline) break;
    const attemptDeadline = Math.min(deadline, Date.now() + timeoutMs);
    try {
      let response = await fetchImpl(CHAT_URL, {
        method:'POST',
        headers:{authorization:`Bearer ${key}`, 'content-type':'application/json', accept:'application/json'},
        signal:AbortSignal.timeout(Math.max(1,attemptDeadline - Date.now())),
        body:JSON.stringify({
          model, messages, max_tokens:192, stream:false,
          ...(model === DEFAULT_NVIDIA_CHAT_MODEL ?
            {temperature:0.95, top_p:1, reasoning_effort:'none'} :
            model === 'nvidia/nemotron-3.5-lightning-30b-a3b' ?
              {temperature:1, top_p:0.95, chat_template_kwargs:{enable_thinking:false}} : {})
        })
      });
      response = await pendingResult(response, key, fetchImpl, attemptDeadline);
      if (!response.ok) {
        lastStatus = response.status;
        if ([401, 402, 403, 429].includes(response.status)) {
          try { await onKeyRejected(key, response.status); } catch { /* Priority persistence is best effort. */ }
        }
        if (response.status === 410) break; // A retired model will not work with another key.
        continue;
      }
      const result = await response.json();
      const answer = result?.choices?.[0]?.message?.content;
      if (typeof answer === 'string' && answer.trim())
        return {answer:answer.trim(), model:result.model || model};
    } catch { /* Timeout, network failure or malformed reply: try the next key. */ }
  }
  const detail = lastStatus ? ` (provider HTTP ${lastStatus})` : '';
  throw Object.assign(new Error(`NVIDIA chat is unavailable with the configured keys${detail}`), {status:503});
}
