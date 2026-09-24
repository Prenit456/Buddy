const sanitize = value => String(value ?? '').replace(/[\t\r\n]+/g, ' ').slice(0, 240);

export function openRouterKeys(value) {
  return [...new Set(String(value || '').split(',').map(key => key.trim()).filter(Boolean))];
}

export async function openRouterConversation({message, history = [], careSettings, keys,
    model = 'openrouter/free', fetchImpl = fetch, timeoutMs = 15000}) {
  const availableKeys = openRouterKeys(keys).slice(0, 3);
  if (typeof message !== 'string' || !message.trim() || message.length > 1000)
    throw Object.assign(new Error('Enter a message of up to 1000 characters.'), {status:400});
  if (!Array.isArray(history))
    throw Object.assign(new Error('Conversation history must be a list.'), {status:400});
  if (!availableKeys.length) throw Object.assign(new Error('OpenRouter conversation keys are not configured'), {status:503});

  const person = careSettings?.person || {};
  const system = `You are Buddy, a calm voice companion for ${sanitize(person.name || 'Friend')}, an older adult. Reply in ${sanitize(person.language || 'English')}. Use plain, respectful language and no more than 55 words. Never diagnose, prescribe, or change medication. For urgent symptoms, falls, danger, or requests for emergency help, clearly advise contacting local emergency services and a trusted person. Do not pretend to be human or replace family. You cannot see today's live calendar, medicine schedule, camera, or device status; direct questions about exact reminders to the Calendar instead of guessing.`;
  const messages = [
    {role: 'system', content: system},
    ...history.slice(-6).filter(item => item && ['user', 'assistant'].includes(item.role) && typeof item.content === 'string')
      .map(item => ({role: item.role, content: sanitize(item.content)})),
    {role: 'user', content: sanitize(message)}
  ];

  // A failed, rate-limited, timed-out or empty reply advances to the next key.
  // At most three 15-second attempts fit within the ESP32's hub timeout.
  for (const key of availableKeys) {
    try {
      const response = await fetchImpl('https://openrouter.ai/api/v1/chat/completions', {
        method: 'POST',
        headers: {authorization: `Bearer ${key}`, 'content-type': 'application/json'},
        signal: AbortSignal.timeout(timeoutMs),
        body: JSON.stringify({model, messages, temperature: 0.35, max_tokens: 512, stream: false})
      });
      if (!response.ok) continue;
      const result = await response.json();
      const answer = result?.choices?.[0]?.message?.content;
      if (typeof answer === 'string' && answer.trim())
        return {answer: answer.trim(), model: result.model || model};
    } catch { /* Try the next configured key. */ }
  }
  throw Object.assign(new Error('OpenRouter conversation is unavailable with the configured keys'), {status:503});
}
