# Use Buddy away from home

Buddy and the website can connect from different cities without a Mac running.
They still need one internet-reachable service to hold accounts, settings and
pairing state and to relay calls. The existing `care-hub/server.mjs` is that
service; `render.yaml` is an optional deployment recipe, not a replacement app.
Wi-Fi alone cannot deliver remote changes to an ESP32 behind a home router.
If you want the website on Vercel, use the same backend described here and
follow the separate [Vercel website guide](VERCEL_DEPLOYMENT.md).

## Recommended prototype host: Render

Use a **paid web service with a persistent disk**. The included Blueprint uses
the 0.5 CPU / 512 MB plan and a 1 GB disk mounted at `/var/data`; the server
reads/writes state there. Review Render's current pricing before creating it.
The free service sleeps and cannot attach the disk, so it is unsuitable for
continuous Buddy sync or durable care-circle data.

1. Put this project in a **private** source repository. Check that `.env.local`,
   `care-hub/state`, and `firmware/include/HubSecrets.h` are not included.
   The current local account data is **not** automatically transferred. Ask for
   a secure migration if you need to keep it; never commit the state file.
2. In Render, create a Blueprint from the private repository's `render.yaml`.
   This creates a **billable** web service and disk. Do not proceed unless you
   agree to Render's charges and have reviewed the prototype limitations below.
3. Enter newly rotated provider keys as Render secret environment variables.
   The Blueprint asks for `ELEVENLABS_API_KEY`, optional
   `ELEVENLABS_API_KEYS`, `GEMINI_API_KEY`, and `OPENROUTER_API_KEYS`.
   Never paste keys into the website or ESP32, and rotate the keys previously
   shared in chat before public deployment.
4. Wait for the site to open at a stable `https://...onrender.com` address.
   Register or sign in there; the localhost site and cloud site have separate
   account databases unless state was deliberately migrated.
5. Configure firmware TLS trust **before** pointing Buddy to the cloud URL.
   Put the verified root CA PEM for the deployed HTTPS certificate into the
   ignored `firmware/include/HubSecrets.h` as `BUDDY_HUB_CA`, for example:

   ```cpp
   #define BUDDY_HUB_CA R"BUDDYCA(
   -----BEGIN CERTIFICATE-----
   ...verified root certificate...
   -----END CERTIFICATE-----
   )BUDDYCA"
   ```

   Do not copy the placeholder, use `setInsecure()`, or guess a certificate.
   Render's actual certificate chain must be checked after the host exists.
   Rebuild and upload `buddy-s3-camera` once with this CA. Certificate renewal
   may require a new firmware build if the trust anchor changes.
6. Join Buddy's **Buddy-Setup** Wi-Fi network and enter home 2.4 GHz Wi-Fi and
   the exact HTTPS app address shown under **My Buddy**. USB is needed for
   flashing/serial setup, not for everyday syncing. Once Buddy reports its
   pairing code, enter it under My Buddy on the *cloud* website. Changing
   server URLs clears the previous device pairing, so pair again if moving
   from localhost/LAN to Render.
7. Verify **ESP32 online**, `Last contact`, and a device revision matching
   the saved revision. Turn Buddy off: the site should show it offline after
   the last-contact timeout. Change eye color while it is offline; when it
   reconnects, wait for the applied revision to catch up and check the TFT.
   A saved change alone is not proof it reached the physical device.

After initial setup, Buddy and each app user only need their own internet
connections. The Mac can be off. If the hosted service is down, remote settings,
cloud speech and calls stop; Buddy's local schedules can continue subject to
its clock and power.

## Important limits before exposing real care data

- This remains an assistive prototype, not a dependable medical or emergency
  system. It needs a security/privacy review, account recovery, backups,
  monitoring and an independent medication/emergency plan before real use.
- Cloud deployment runs on Linux. The existing prescription **photo** reader
  uses macOS Vision and will not work there; entering reviewed prescription
  text remains possible. If all configured voice providers fail, macOS `say`
  is not available as a cloud fallback.
- Buddy video/audio currently relays through the service. Its actual latency,
  bandwidth and hosting cost need testing on both home and mobile networks.
- This setup does not make a USB-connected ESP automatically provision itself.
  The first firmware flash and CA configuration are separate from Wi-Fi setup.

Render references: [web services](https://render.com/docs/web-services),
[persistent disks](https://render.com/docs/disks),
[Blueprint configuration](https://render.com/docs/blueprint-spec), and
[pricing](https://render.com/pricing).
