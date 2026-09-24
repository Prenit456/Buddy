# Buddy care circles

## Connect the real Buddy

1. Start `BUDDY_HOST=0.0.0.0 npm start` and open http://localhost:4173/.
2. Register as **I'm using Buddy**. This account owns the care circle and controls access.
3. Connect the ESP32-S3 to the same Wi-Fi, set the Mac's LAN Care Hub URL in Buddy's setup portal, and enter Buddy's pairing code in **My Buddy**.
4. In **My people**, create a one-use invitation. Register a supervisor in a separate tab/browser and join using that code. Sessions are tab-specific, so two roles can be demonstrated together.
5. The supervisor edits Calendar or Voice & settings. Both apps see the same saved version. **My Buddy** shows whether the ESP32 has acknowledged the revision; offline changes stay queued.
6. The supervisor calls Buddy from **My Buddy**. Accept on the physical Buddy with “Hey Buddy answer call.” Connect media explicitly on both sides.
7. Call in the other direction: “Hey Buddy call Priya” (use the supervisor's registered name). The supervisor's app rings.
8. Try a camera check-in. Only supervisors given camera permission can request one. The older person must accept; it is microphone-free, visible, and stops after two minutes or earlier on hang-up/privacy revocation.
9. Request a wellbeing check-in, send a family message, claim a shared errand, or acknowledge “I'm responding” on an alert. Messages to Buddy are queued for up to ten minutes. The check-in deadline becomes an alert if no reply arrives.

## Physical device setup

- The assumed target remains ESP32-S3 N16R8: 16 MB **flash**, 8 MB **PSRAM**. A label saying “16 MB” does not establish available RAM. Confirm the real board before flashing.
- The existing TFT, microphone and amplifier pin assignments are in HardwareConfig.h and platformio.ini.
- Buddy's OV7670 camera uses the September 23 pin map in CameraConfig.h. Select `buddy-s3-camera` for the complete assembly; it converts RGB565 to JPEG in software. Camera initialization checks pin collisions and reports actual initialization success to the app. Confirm the module memory suffix and TFT driver before upload; see FLASHING.md.
- In the Wi-Fi portal, enter the Care Hub's reachable URL. `localhost` on the ESP32 means the ESP32 itself, so use your laptop/server's LAN address.
- Alternatively, visit Buddy's local IP. Sign in with username `buddy` and its random per-boot Serial Monitor setup PIN. Only the pairing code and server setup are available locally; the account app lives on the hub.
- Enter the ten-minute pairing code in **My Buddy → Pair Buddy** using the older person's account. A private device token is persisted in NVS; provider keys never go on the device.
- HubSync performs network I/O on a worker. Validated settings must be saved to LittleFS before the main loop acknowledges their revision. Reminders, SOS and eyes stay on the main loop.
- Local events and command acknowledgments use a bounded, persisted outbox. The hub deduplicates event IDs. The device reports overflow/storage errors; it is not unlimited offline archival. Stale offline call requests are not dialed later.
- Calls pause cloud wake transcription. If a physical SOS button is installed and assigned, a **short press hangs up**; a 1.2-second hold starts SOS and ends the call. The current wiring has no assigned button, so use app hang-up. Voice hang-up during a live microphone call is not implemented as an offline recognizer.
- Camera calls show a steady red camera indicator. Incoming video is scaled to fit below the TFT clock/date/battery header in either orientation. Camera check-ins have no microphone audio.

## Network and privacy boundaries

The hub must be running and reachable for remote sync, calls and alerts. Saved schedules keep running offline with a valid clock. This is a self-hosted prototype, not an already deployed cloud service.

Browser camera/microphone requires a [secure context: HTTPS or localhost](https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/getUserMedia). Local LAN HTTP is useful for ESP32 transport testing but exposes credentials and media to that network. Do not use it on public Wi-Fi.

For HTTPS, supply BUDDY_TLS_CERT and BUDDY_TLS_KEY or place the hub behind a trusted HTTPS proxy. The ESP32's BUDDY_HUB_CA must contain the trusted PEM root certificate. It fails closed if HTTPS is configured without a CA. Do not forward plain device ports publicly.

App-to-app video uses [WebRTC signaling](https://developer.mozilla.org/en-US/docs/Web/API/WebRTC_API/Signaling_and_video_calling). BUDDY_ICE_SERVERS accepts a JSON array of STUN/TURN configurations. A [TURN relay is usually needed across restrictive networks](https://webrtc.org/getting-started/peer-connections); none is silently purchased or configured. Without it, same-LAN calls may work while cross-network calls fail.

Buddy-to-app media uses authenticated HTTP exchanges of 320×240 JPEG frames and 16 kHz mono signed little-endian PCM. It is a lightweight low-frame-rate prototype, **not a full ESP32 WebRTC stack or a verified high-quality intercom**. The hub relays it in memory, so it is not end-to-end encrypted beyond transport TLS. No frames/audio are written to disk; call metadata is retained. [Espressif's camera driver](https://github.com/espressif/esp32-camera) supplies capture; [TJpg_Decoder](https://github.com/Bodmer/TJpg_Decoder) decodes the return video on the TFT.

Wake speech and general conversation are separate: short utterances go to ElevenLabs for transcription before the C++ wake-phrase check; general questions go to NVIDIA's configured chat model. The hub tries the next configured NVIDIA key when a request fails or returns no text. Live call audio does **not** go through either provider. This project does not promise offline/custom local keyword spotting.

## What is implemented versus still required

Implemented: roles, circle isolation, invitation and per-supervisor permissions, password hashes, hashed session/device tokens, optimistic settings versions, pairing, sync acknowledgments, C++ wake-call commands, WebRTC signaling/client, PCM/JPEG relay/client, ESP32 adapters, consent/call expiry, shared tasks, alert claims, check-ins, family messages, calendar and prescription review.

Still required before real elder-care use:

- Exact board/camera/pins, physical flashing, battery calibration, microphone gain/echo cancellation, room tests, power reliability and offline clock hardware.
- End-to-end live audio/video testing on hardware and across the intended networks. The HTTP relay is latency-sensitive and has no hardware acoustic echo canceller.
- Background push notifications/ringing, reliable emergency escalation and delivery acknowledgment, supervisor on-call rotations, and consent/accessibility testing with real users. Currently the app must stay open.
- Production identity verification, password recovery, device revocation/re-pair workflow, encrypted backups/storage, account export/deletion, audit retention, stronger rate/resource limits, monitoring, and security review. Accounts in this prototype are local, email is not verified.
- Standalone mobile-store packaging and public hosting if desired. This is a responsive web app.
- Camera person tracking/fall detection: not implemented. Having a camera does not by itself make these reliable.

User confirmations indicate reported actions, not proof of taking medicine. OCR always requires review of the original prescription; unclear doses/times must not be guessed. Buddy is not a medical device or emergency service.
