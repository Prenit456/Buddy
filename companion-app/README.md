# Buddy website

This is the current user/carer interface, shared by the localhost preview and
Vercel deployment. The main public website is https://buddyv2.vercel.app.

The current UI uses username/password accounts, private care circles, reviewed
prescription/calendar entries, messages, device settings, a TFT eye preview and
consent-based call controls. Hardware requests and account data use the existing
Cloudflare care API; the browser does not need a local CareHub or Render server.

Build from the repository root with `npm run build:vercel`. Vercel's `/api/v2/*`
rewrite forwards to `https://buddy-remote-test.prenit-kathuria.workers.dev`.
Provider/device credentials stay on the backend, not in these static files.

See [the current deployment guide](../docs/VERCEL_DEPLOYMENT.md) for routing,
release checks and the separate live-hardware verification steps. An offline
device must not be described as connected just because the website loads. Call
buttons and interface screenshots are not proof of measured video-call quality.

This is an assistive prototype, not a medical or emergency service. Medication
reminders require human review; a response does not verify medicine ingestion.
