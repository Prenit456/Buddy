# Buddy local API

The Care Hub normally uses http://localhost:4173. Account operations require bearer authentication and circle membership. No virtual-device API is available.

## Current care-circle API

`POST /api/v2/auth/register` takes name, email, password and role (`user` or `supervisor`). `POST /api/v2/auth/login` returns a session token; use `Authorization: Bearer <token>`. `GET /api/v2/me` returns the account, circles and ICE configuration. `POST /api/v2/auth/logout` invalidates that session.


`/api/v2/circles/:id` is membership-scoped. Owner-only: POST `/invite`, POST `/members`, PUT `/privacy`, POST `/devices/claim`. Supervisors join using POST `/api/v2/join` with a one-use code. Circle endpoints `/tasks`, `/alerts`, `/checkins`, `/messages`, `/calls` implement the corresponding app features; permissions are enforced server-side.

Legacy editor functions are bridged under `/api/v2/circles/:id/bridge/api/...`. GET settings includes `_revision`; PUT must supply the same version or returns 409. The current editor uses prescription scan → review → settings save (not the old convenience import route).
The signed-in website's **Talk with Buddy** screen POSTs to `/api/v2/circles/:id/bridge/api/assistant` with a message and at most the recent conversation turns. It requires circle membership, validates input, and gives a clear 503 when the AI provider is unavailable. The browser keeps its transcript only in the current tab; it does not write chat to the care journal.

Devices enroll through POST `/api/v2/device/enroll`, receive a private deviceToken and short pairing code, then authenticate `/device/sync`, `/device/media`, `/device/tts`, `/device/transcribe`, `/device/assistant`. Sync uploads appliedRevision, capabilities, status, event IDs and acknowledged command IDs; it returns newer settings and queued commands. The device acknowledges the settings revision only after saving locally. Provider keys stay on the hub.

Calls: POST circle `/calls` takes `to` and `kind` (`audio`, `video`, `camera`). Only the recipient may POST `/api/v2/calls/:id/accept`; either participant may `/end`. Only accepted participants may exchange `/signal` (WebRTC offer/answer/candidate) or `/media` (PCM/JPEG). Device media is scoped to its assigned call. Camera check-ins forbid audio and expire after two minutes.

See `care-hub/hub_tests.mjs` for executable request examples and permission checks. See [care-circle deployment boundaries](CARE_CIRCLES.md) before using real care data.

## Bridged editor routes

| Method | Route | Purpose |
|---|---|---|
| GET | /api/status | ESP32 heartbeat, capabilities and applied settings revision |
| GET / PUT | /api/settings | Read or validate/save complete configuration |
| GET | /api/calendar?month=YYYY-MM | Scheduled dates and reminder status |
| GET | /api/journal | Persistent care events |
| POST | /api/actions | Queue a supported action for the paired ESP32 |
| POST | /api/transcribe | Companion only: short WAV to ElevenLabs STT |
| POST | /api/tts | Companion only: text to speech |
| GET | /api/tts/voices | Companion voice profiles |
| POST | /api/assistant | Companion only: NVIDIA conversation with key fallback |
| POST | /api/prescription/scan | Companion: JPEG/PNG or text → unconfirmed draft |
| POST | /api/prescription/review | Validate reviewed items; return reminders without saving |

The caregiver app uses scan/review on the Care Hub, then saves the resulting reminders to its care circle. The ESP32 applies them on its next sync.

## Voice

The ESP32 microphone sends speech to the hub's authenticated device transcription endpoint. Wake-phrase matching, SOS priority and reminder actions occur in the firmware C++ core.

/api/transcribe accepts {"audio":"data:audio/wav;base64,..."} for short 16 kHz, mono, 16-bit PCM WAV utterances. The companion holds ELEVENLABS_API_KEY.

/api/tts accepts text, voiceId, language, rate, and optional format:"wav" for the ESP32. The normal cloud response is MP3; the hardware response is mono 16 kHz PCM WAV. X-Buddy-TTS-Engine reports the actual renderer. macOS system speech is a fallback if cloud TTS fails.

Prescription review requires confirmed:true, nonempty title, explicit valid times, and startDate/endDate for every selected item. Schedule capacity is 64 time entries, not 64 medicines with unlimited doses.

## Hardening before deployment

Authentication/pairing, payload/rate limits, optional TLS and settings acknowledgments are present in v2. Production still needs encrypted storage/backups, account recovery/revocation, stronger abuse protection, reliable background alert delivery, retention controls, reliable timekeeping and physical testing. No API treats a webhook acknowledgment as proof a caregiver responded.
