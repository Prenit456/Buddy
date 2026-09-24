# Put the existing Buddy website on Vercel

This is the **same** Buddy website in `companion-app`, not a new design.
Vercel serves its pages and forwards `/api/*` to the persistent Buddy backend.
The ESP32 connects directly to that backend over HTTPS. A Vercel deployment
**alone** cannot run this app's persistent accounts, pairing, device polling,
and call relay.

## 1. Make a private GitHub repository

The project folder is not yet a Git repository. Create an **empty private**
repository on GitHub, then in Terminal run these commands from the project
folder. Replace `YOUR_USERNAME` and `YOUR_REPOSITORY` with the values GitHub
shows you.

```sh
cd /Users/preetib/Desktop/ideathon
git init
git add .
npm run check:repo
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

## 2. Host the backend first

Deploy the *same private repository* to an always-on backend host. The provided
`render.yaml` is the current prototype option; see
[cloud setup](CLOUD_DEPLOYMENT.md). Use a paid service with a persistent disk,
and set the provider keys there, **not in Vercel**. Wait for its public HTTPS
address, such as `https://your-buddy-backend.onrender.com`. Test that opening
that address displays the Buddy sign-in page. The backend must remain online
even when your Mac is off.

If you use a non-Render backend, set its `BUDDY_PUBLIC_URL` to its HTTPS origin
so the pairing screen displays the direct ESP32 address.

## 3. Deploy the website to Vercel

1. In Vercel, choose **Add New → Project → Import Git Repository** and select
   the private Buddy repository. Keep **Root Directory** at the repository root.
2. Set the environment variable `BUDDY_BACKEND_URL` to the backend's HTTPS
   **origin only**—for example `https://your-buddy-backend.onrender.com`.
   No trailing path, `/api`, username, or credentials. Set it for Production
   and Preview if you use preview deployments.
3. Deploy. `vercel.mjs` uses this variable to forward API traffic; the build
   intentionally fails with an explanatory error when it is missing. Vercel
   runs `npm run build:vercel` and serves `dist/vercel`.
4. Open the Vercel URL. Register/sign in and check **My Buddy**. It should show
   the **backend** HTTPS URL for ESP32 setup, not the Vercel site URL. On the
   backend, create/pair the physical device following the cloud setup guide.
5. Test one setting: save an eye-color change, then wait for the ESP32's
   *applied revision* to equal the saved revision. Turn Buddy off and verify
   it is shown offline after the last-contact timeout.

Do not point Buddy at `localhost` or at a Vercel page with no backend. The
website and Buddy can be in different cities only while the backend is up and
both devices have internet. If you change `BUDDY_BACKEND_URL`, redeploy Vercel;
moving to a different backend database requires account/data migration and
device re-pairing.

The current prescription **photo** OCR uses macOS Vision and does not run on
Render's Linux host; reviewed text entry still works. Buddy is an assistive
prototype, not a replacement for medication or emergency care.

Vercel references: [programmatic configuration](https://vercel.com/docs/project-configuration/vercel-ts),
[external rewrites](https://vercel.com/docs/routing/rewrites), and
[deploying from Git](https://vercel.com/docs/deployments/git).
