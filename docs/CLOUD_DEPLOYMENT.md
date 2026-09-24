# Buddy free-tier cloud prototype

The existing Buddy website runs on Vercel; the Node backend runs on Render
Free; Supabase stores accounts, settings, pairing data and provider key-order
fingerprints. The ESP32 contacts Render directly. The Mac can be off after the
initial firmware upload, but the free services are **not dependable care
infrastructure**. Render can sleep after inactivity and takes time to wake;
Supabase can pause low-activity projects. Keep an independent emergency and
medication plan.

## 1. Prepare Supabase

Create a Supabase Free project. In its **SQL Editor**, run the complete
[`SUPABASE_SETUP.sql`](SUPABASE_SETUP.sql) once. The table has row-level security
enabled, no browser-user access policy, and no `anon` or `authenticated` grant.
The Buddy backend continues to manage its own user accounts; Supabase Auth is
not used by this prototype.

In the Supabase dashboard, find the **Project URL** (an HTTPS origin) and
create a **secret** API key starting `sb_secret_`. Supabase's publishable key
will not work for server storage. Do not share the secret key, paste it in
chat, put it in the browser/ESP32, or commit it to Git. The database password
is not needed by Buddy's HTTP Data API.

The cloud database starts empty. Existing accounts in your Mac's
`care-hub/state` do **not** transfer automatically. Register new accounts on
the cloud site, or arrange a carefully reviewed migration before using real
data. Supabase Free does not provide automatic database backups; export test
data if it matters to you.

## 2. Deploy the Render Free backend

Push the updated repository to GitHub. In Render choose **New → Blueprint**,
connect the private Buddy repository, and select its root `render.yaml`. Check
the review screen: the web service must say **Free**, with **no persistent
disk**. Do not approve a paid plan accidentally.

Set these values in Render's private environment-variable prompts:

- `SUPABASE_URL`: the Supabase Project URL, for example
  `https://example.supabase.co` (no path or trailing `/api`).
- `SUPABASE_SECRET_KEY`: the `sb_secret_` server key.
- `ELEVENLABS_API_KEY`: a newly rotated key with speech-to-text and TTS
  permission. Optional `ELEVENLABS_API_KEYS` supplies backups.
- `NVIDIA_CHAT_API_KEYS`: one or more newly rotated NVIDIA API Catalog keys,
  separated by commas (or new lines), for the AI chatbot. The default chat
  model is `meta/muse-glimmer-30b`; optional
  `NVIDIA_CHAT_MODEL` overrides it. This does not replace speech keys.
- `GEMINI_API_KEY`: optional Google speech fallback after ElevenLabs TTS fails.

If this Blueprint was already deployed with `OPENROUTER_API_KEYS`, add
`NVIDIA_CHAT_API_KEYS` manually under **buddy-care → Environment**, remove the
old OpenRouter variable, and select **Save and deploy**. Blueprint `sync: false`
values are prompted only at initial creation, not again on later syncs.

The Blueprint already sets `BUDDY_STORAGE=supabase` and `BUDDY_HOST=0.0.0.0`.
In cloud mode, storage is required: if the Supabase table, URL or secret key
is missing, Buddy refuses to start rather than silently writing disposable
local files. Watch the Render deploy logs for a successful start, then open
its `https://…onrender.com` URL. It should show Buddy's sign-in page. Do not
paste secret-bearing logs into a public issue or chat.

The Render instance's in-memory call relay, presence and rate-limit state
reset when it sleeps or restarts; calls in progress will drop. The single JSON
database row is suitable for a small prototype, not high-concurrency use.
AI/TTS provider usage, bandwidth and other plan limits may still cost money
or interrupt service; review each provider's current terms.

## 3. Deploy Vercel and configure Buddy

Follow the [Vercel website guide](VERCEL_DEPLOYMENT.md). Set only
`BUDDY_BACKEND_URL` there, to the Render HTTPS origin. **Never** add the
Supabase secret or provider keys to Vercel's website code.

Before pointing the ESP32 to Render, verify the Render HTTPS certificate's
root CA and add its PEM to the ignored `firmware/include/HubSecrets.h` as
`BUDDY_HUB_CA`. Never use a placeholder certificate or `setInsecure()`.
Rebuild/upload `buddy-s3-camera`. In Buddy's **Buddy-Setup** Wi-Fi portal,
enter home 2.4 GHz Wi-Fi and the exact HTTPS backend address shown under
**My Buddy** on the hosted site. Enter Buddy's pairing code on the cloud site.
Moving from a previous local server requires re-pairing; old local data is
not automatically present on Supabase.

Verify an eye-color change by waiting for the device's **applied revision**
to match the saved revision and checking the TFT. Turn Buddy off and confirm
the site reports it offline after its last-contact timeout. Then test speech
and calls on real networks. A saved website setting alone is not proof that
the hardware received it.

## Limits

This remains an assistive prototype, not a dependable medical or emergency
system. The prescription **photo** reader uses macOS Vision and cannot run on
Render's Linux host; reviewed text entry still works. Phone-to-phone WebRTC
across arbitrary networks may require a TURN server; device calls relay via
Render and must be load-tested on the free CPU/bandwidth limits. Free-tier
sleep, pausing, key quotas and a lack of automatic backups are additional
risks. Rotate any API keys previously shared in chat before hosting.

Official references: [Render Free](https://render.com/docs/free),
[Supabase API keys](https://supabase.com/docs/guides/getting-started/api-keys),
[Supabase project pausing](https://supabase.com/docs/guides/platform/free-project-pausing),
and [Vercel Git deployment](https://vercel.com/docs/git).
