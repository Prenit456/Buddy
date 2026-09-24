# Buddy — care that keeps people independent

One app with **user and supervisor accounts** and an ESP32-S3 firmware project. The website connects only to a paired physical Buddy.

## Run it

Requirements: Node.js 20+. Photo OCR additionally uses Apple's Vision framework and Swift on macOS.

    cd /Users/preetib/Desktop/ideathon
    BUDDY_HOST=0.0.0.0 npm start

- [Buddy app](http://localhost:4173/): register as a user or supervisor. **Talk with Buddy** provides a typed AI conversation; supervisors can start a **Video call Buddy** from Today when the paired device reports working camera and audio.
- The user opens **Care circle** to invite supervisors, and **My Buddy** to pair the ESP32.
- The signed-in app keeps the original Buddy website's dark-teal sidebar, warm cards, and settings editor. Calendar, prescription review and voice settings remain inside that same app; chat and calling are additional screens, not a replacement website.

Keep that terminal running. Settings and care history are saved under `care-hub/state`. The server binds only to localhost unless `BUDDY_HOST` is explicitly changed.

For access from another city **without leaving the Mac on**, the same app and
hardware-sync service must run on an always-on internet host. The optional
[Render cloud setup](docs/CLOUD_DEPLOYMENT.md) prepares that deployment; it has
not been deployed or paired for you. Hosting is billable and the prototype
needs further security/reliability work before storing real care data online.
You can serve the existing website from GitHub through Vercel while the
persistent backend stays on Render; follow the
[Vercel deployment guide](docs/VERCEL_DEPLOYMENT.md).

The Care Hub no longer offers virtual devices or legacy demo mode. Pair the
ESP32 in **My Buddy**, and wait for its device revision to match the saved
revision. The site reports the physical device's last contact time; an open
website by itself does not make Buddy appear online.

New care circles start with empty calendars and contacts. Earlier single-device settings and journal files remain untouched in `care-hub/state`; they are not shared with newly registered accounts. Accounts and circles live in `care-hub.json` there (ignored by Git, mode 0600).

See [hardware pairing and calls](docs/FLASHING.md) for setup details.

## What's implemented

- User-owned circles with multiple invited supervisors, individual edit/call/camera permissions, one-use invitations, password hashing and revocable access.
- Device enrollment, ten-minute pairing codes, versioned Wi-Fi settings sync and actual device-applied revision tracking. Offline changes stay queued on the hub.
- App-to-app audio/video calling via WebRTC. Buddy-to-app calls use an ESP32-friendly PCM/JPEG relay. The camera adapter is for **Buddy's existing camera**, not a required separate phone.
- Consent-first, microphone-free, two-minute camera check-ins; visible camera indication and a user-controlled privacy pause. No media recordings are saved.
- Shared care tasks, claimed alerts ("I'm responding"), check-in requests with overdue alerts, family messages and a settings/access audit trail.
- Colored, procedural [FluxGarage RoboEyes](https://github.com/FluxGarage/RoboEyes), with blinks, gaze, expressive speaking/listening/reminder/SOS states and a quiet nighttime expression.
- Native 320×240 landscape **and** 240×320 portrait RGB565 output. The browser decodes frames produced by the same C++ renderer used by the TFT; it does not recreate the eyes in JavaScript.
- Large eyes; only date, time and battery across the top. Four selectable base colors. Backlight control is supported by the design but fixed with the current direct-3V3 wiring; no battery sensor is connected.
- No Talk or Done button on Buddy. The design supports a **top SOS button** held for 1.2 seconds, but this assembly has no assigned SOS-button GPIO. Voice/app SOS remains available with connectivity.
- Configurable call name, for example “Hey Momo”. English voice commands for the day's agenda, clock/date, confirmation, ten-minute snooze, repeat, water check-ins, wellbeing and SOS.
- Calendar with one-off, daily and weekly schedules, inclusive start/end dates, up to 64 scheduled times, and a day-by-day agenda evaluated by C++.
- Multiple simultaneous reminders queue instead of replacing one another. Unconfirmed reminders can repeat up to three times, a minute apart. Quiet hours silence non-medicine reminders; SOS and medication reminders remain audible.
- Prescription photo → local OCR → editable review → confirmed calendar entries. Explicit written clock times are extracted; ambiguous frequency, dose and duration are never guessed.
- Care journal with confirmations, snoozes, missed reminders, hydration and wellbeing; downloadable JSON history.
- Optional water, gentle-movement, social-connection and evening-check-in routines.
- Care contacts, missed-reminder/SOS webhook configuration, family voice messages, timezone controls and natural voice/pace/volume settings.

## Hands-free speech

On the device preview, click **Enable hands-free listening once** and grant microphone access. After that, use the wake phrase, without holding a Talk button. A wake phrase by itself opens a 12-second follow-up window. The page must remain open and active; browsers can suspend background tabs.

Voice activity is detected locally, but **short speech clips are sent to ElevenLabs before C++ checks the wake phrase**. This is not private, offline keyword spotting. It needs internet, a working ELEVENLABS_API_KEY and provider credits. Stop listening with the microphone control. Buddy pauses recording during its own speech and stops after repeated recognition-service errors.

For spoken replies, ElevenLabs is tried first using the primary key and then the configured backup keys. A quota- or rate-limited key moves to the end of the list for future requests; Google Gemini TTS runs only after all ElevenLabs keys fail. NVIDIA Speech NIM and the local macOS voice remain later fallbacks. The three additional ElevenLabs keys currently return `missing_permissions` for TTS; enable Text to Speech for them before relying on them as backups.

Examples:

- “Hey Buddy, what is on today?”
- “Hey.” (Then say a question within 12 seconds. This short form still needs
  cloud transcription to recognize the wake word; it is not an offline hotword.)
- “Hey Buddy, I have taken it.”
- “Hey Buddy, remind me later.”
- “Hey Buddy, I drank water.”
- “Hey Buddy, I feel okay.”
- “Hey Buddy, call for help.”
- “Hey Buddy, call Priya.” (Priya must be an invited supervisor.)
- “Hey Buddy, answer call.” / “Hey Buddy, decline call.”
- “Hey Buddy, allow camera.” / “Hey Buddy, stop camera.”

The device's typed input exercises the same C++ parser. The website's **Talk with Buddy** screen and general device conversation use OpenRouter through the Care Hub; reminders and built-in commands do not require a conversation model. The current configuration selects [OpenRouter's free model route](https://openrouter.ai/openrouter/free), which has rate limits and can route to different models. Two verified keys are kept in order for retry. The speech provider and conversation provider are separate. Website chat history stays in the current browser tab and is not saved to the care journal.

September 23 live checks: Gemini speech and ElevenLabs speech/transcription passed.
The prior NVIDIA conversation endpoint timed out. An authenticated Care Hub
conversation request now succeeds through OpenRouter using synthetic test data.
See [verification results](docs/VERIFICATION.md).

Keep keys only in the ignored .env.local file; see .env.example. Rotate any keys pasted into chat before deploying. No provider key belongs in the browser or ESP32 firmware.

## Prescription safety

Use a clear JPEG or PNG, preferably printed text. Handwriting can be inaccurate. The original photo is processed locally on the Mac companion, and its temporary server file is removed after processing. No photo is sent to NVIDIA or ElevenLabs.

Review every medicine name, dose, instruction, time, start date and end date against the original prescription before selecting **Add reviewed items to calendar**. Buddy stores reminders; it does not decide a treatment schedule. Unclear instructions must be confirmed with the prescriber or pharmacist. A confirmation records what the user reported, not verified medication ingestion.

## ESP32 builds

For the assembled September 23 wiring, use the complete `buddy-s3-camera`
environment and follow [the firmware upload guide](docs/FLASHING.md). The source
now uses the actual shared I2S clocks and OV7670 pin map. No touch or display
readback is enabled; the directly powered backlight has fixed brightness.

Confirmed board memory: ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM). The build selects an ILI9341 SPI TFT; confirm that controller against your successful display test before flashing. The microphone appears to match the configured I2S wiring.

    cd firmware
    pio run -e buddy-s3-camera
    pio run -e buddy-s3-camera --target upload

- buddy-s3: display, local schedule and pairing/settings sync, without network audio.
- buddy-s3-voice: INMP441 + MAX98357A network speech and call-audio adapter. The companion holds keys; C++ on the ESP32 makes command decisions.
- buddy-s3-camera (default): adds OV7670 RGB565 capture/software JPEG conversion and incoming video on the TFT, using the wiring supplied in this task.
- buddy-s3-sam: older compact offline robotic speech fallback. Do not combine it with network voice.

For pairing, enter the Care Hub address in Buddy's Wi-Fi setup portal, or visit its local IP using username `buddy` and the per-boot setup PIN printed in Serial Monitor. Enter its pairing code in the user's app. The account app runs on the Care Hub, not inside the ESP32; uploading its web filesystem is unnecessary in the new mode. The old unauthenticated local settings endpoints are disabled by default.

For a **trusted private LAN**, run the companion with BUDDY_HOST=0.0.0.0 and use the laptop's LAN IP with port 4173. Phone camera/microphone require HTTPS. HTTPS server certificate options are in .env.example; the ESP32 requires the matching root CA in HubSecrets.h and does not bypass certificate validation. Do not publish this prototype without the additional hardening listed in the care-circle guide.

[Hardware details](docs/HARDWARE.md) · [Architecture and parity](docs/ARCHITECTURE.md) · [API](docs/API.md) · [Verification](docs/VERIFICATION.md)

## Tests

    npm test
    npm run test:integration

The first checks the website and service logic. Integration tests exercise hardware pairing, settings sync and calls with a temporary fictional state directory and no provider keys. They do not overwrite your care settings. Firmware can be built separately with `bash scripts/firmware.sh build`.

`npm run test:providers` checks the current authenticated device voice endpoints
with temporary fictional accounts and synthetic audio. It uses provider credits;
it does not alter your keys or saved care-circle data.

## Boundaries to understand

This is an assistive prototype, not a certified medical or emergency-response device. App calls require invited accounts and connectivity; phone-number contacts do not enable cellular calls or SMS. An SOS alert or webhook is not confirmation that someone has responded. The app must be open for incoming calls/alerts; background push is not implemented.

Schedules and completed-occurrence history persist. Timed snoozes and the active queue are currently RAM state; after restart, only scheduled occurrences up to 20 minutes late are reconsidered. An offline cold boot needs a valid clock (NTP, browser time sync or a future RTC adapter) before reminders can run. Use an independent medication/emergency backup.

TFT colors, viewing angle and transport latency depend on the physical panel. The user reports successful individual component tests; operation of the combined firmware on the assembled hardware remains to be checked. The current wiring has no assigned physical SOS button or controllable backlight pin; see the upload guide for those limitations.

RoboEyes is GPL-3.0-or-later. Its license, attribution and modification note are included in firmware/lib/BuddyDisplay/vendor. Review those obligations before distributing combined firmware.
