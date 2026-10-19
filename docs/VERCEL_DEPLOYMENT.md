# Current Buddy website release: Vercel + Cloudflare

The current website lives in `companion-app`. Vercel serves its static pages;
accounts, care circles, pairing, settings, messages and call media use the
existing Cloudflare Worker:

`https://buddy-remote-test.prenit-kathuria.workers.dev`

The ESP32 contacts that Worker directly. The app and hardware can use different
Wi-Fi networks. No local CareHub or Render service is needed for this build.

## Current release procedure

1. Run `npm run build:vercel` and `node --test scripts/vercel-release.test.mjs`.
2. Review the website changes before committing. Do not use `git add .` in this
   workspace: it also contains separate hardware experiments, local credentials
   and submission material. Publish only reviewed website/release files.
3. Keep the repository root as Vercel's Root Directory, with no framework preset.
   The build command is `npm run build:vercel`; output is `dist/vercel`.
4. Deploy the reviewed production-branch commit. Confirm its source commit and
   successful build in Vercel, then check the public website and API endpoints.
5. Verify `/settings`, `/device-preview.js`, `/api/health` and `/api/v2/me`.
   Without credentials, `/api/v2/me` should return an authentication error, not
   an old pairing-only response, HTML failure or unavailable backend.

`vercel.json` forwards `/api/v2/*` to the matching Cloudflare care API and
`/api/health` to `/health` on the Worker. API responses are not cached. The
service worker version bump refreshes the frontend on existing installations.
The static build includes the current username login, calendar, prescription
review, care-circle/call controls, hardware controls and eye preview.

The source repository is `https://github.com/Prenit456/Buddy`. The confirmed main
public website is `https://buddyv2.vercel.app`. Another project,
`buddy-sigma-ashy.vercel.app`, is connected to the same repository; pushing their
shared production branch may build both. This release does not change repository
visibility, database contents, credentials or hardware pairing.

Provider keys, device credentials and D1 bindings belong in Cloudflare, never in
the static app or GitHub. Keep `.env*`, `.pio`, private care data and cloud auth
files out of commits. A successful website build alone does not prove hardware
calls or delivery; verify with a paired device separately. Final video
performance still needs combined camera, display, audio and network testing.

Official references: [Vercel rewrites](https://vercel.com/docs/routing/rewrites),
[deploying Git repositories](https://vercel.com/docs/git).

---

# Legacy Render/Supabase setup (historical reference only)

The instructions below describe the older architecture. They do not apply to
the current Cloudflare production release above. Do not point the current
website back to Render or upload the whole working folder.

This is the **same** Buddy website in `companion-app`, not a new design.
Vercel serves its pages and forwards `/api/*` to the Buddy backend.
The ESP32 connects directly to that backend over HTTPS. A Vercel deployment
**alone** cannot run this app's persistent accounts, pairing, device polling,
and call relay.

## 1. Make a private GitHub repository

If this folder is not already a Git repository, create an **empty private**
repository on GitHub. Run the preparation commands first, then stop and read
the safety-check result before running any commit or push command:

```sh
cd /Users/preetib/Desktop/ideathon
git init
git add .
npm run check:repo
```

Only if the check says **no known private paths or key patterns**, review the
staged files and then continue. Replace the example GitHub URL with your own:

```sh
git status --short
git commit -m "Prepare Buddy website for hosting"
git branch -M main
git remote add origin https://github.com/YOUR_USERNAME/YOUR_REPOSITORY.git
git push -u origin main
```

Stop if `check:repo` reports a sensitive file. It checks staged files, but it
cannot guarantee every secret is absent. Review `git status` yourself before
pushing. Do **not** upload `.env.local`, `care-hub/state`,
`firmware/include/HubSecrets.h`, `.pio`, or `dist` through GitHub's web uploader.
Credentials pasted in chats should be rotated before hosting.
If `git remote add origin` says the remote already exists, do not repeat it;
check `git remote -v`. GitHub account passwords do not work for HTTPS Git
pushes. Authenticate with GitHub Desktop, GitHub CLI, or a personal access
token; never put a token in a command or share it in chat.

## 2. Host the backend first

Deploy the *same private repository* to Render Free after completing the
[Supabase cloud setup](CLOUD_DEPLOYMENT.md). Supabase persists accounts and
device state; Render's local filesystem is disposable. Set provider keys on
Render, **not in Vercel**. Wait for its public HTTPS address, such as
`https://your-buddy-backend.onrender.com`. Test that opening that address
displays the Buddy sign-in page. It can run when your Mac is off, but the free
instance can sleep or restart and is not an always-available care service.

If you use a non-Render backend, set its `BUDDY_PUBLIC_URL` to its HTTPS origin
so the pairing screen displays the direct ESP32 address.

## 3. Deploy the website to Vercel

1. In Vercel, choose **Add New → Project → Import Git Repository** and select
   the private Buddy repository. Keep **Root Directory** at the repository root.
2. The repository's `vercel.json` already forwards `/api/*` to Buddy's Render
   backend at `https://buddy-care-rat3.onrender.com`. Do not add provider keys,
   Supabase secrets, or `PORT` to Vercel. The old `BUDDY_BACKEND_URL` variable
   is no longer needed and may be removed.
3. Deploy. Vercel runs `npm run build:vercel` and serves `dist/vercel`.
4. Open the Vercel URL. Register/sign in and check **My Buddy**. It should show
   the **backend** HTTPS URL for ESP32 setup, not the Vercel site URL. On the
   backend, create/pair the physical device following the cloud setup guide.
5. Test one setting: save an eye-color change, then wait for the ESP32's
   *applied revision* to equal the saved revision. Turn Buddy off and verify
   it is shown offline after the last-contact timeout.

Do not point Buddy at `localhost` or at a Vercel page with no backend. The
website and Buddy can be in different cities only while the backend is up and
both devices have internet. If the Render backend URL changes, update the
external rewrite destination in `vercel.json`, commit and push, and redeploy;
moving to a different backend database requires account/data migration and
device re-pairing.

The current prescription **photo** OCR uses macOS Vision and does not run on
Render's Linux host; reviewed text entry still works. Buddy is an assistive
prototype, not a replacement for medication or emergency care.

Vercel references: [static configuration](https://vercel.com/docs/project-configuration/vercel-json),
[external rewrites](https://vercel.com/docs/routing/rewrites), and
[deploying from Git](https://vercel.com/docs/deployments/git).
