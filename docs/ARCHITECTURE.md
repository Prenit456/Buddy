# Hardware-only architecture

## Shared C++

firmware/lib/BuddyCore contains the reminder queue, date/weekday matching, wake-name parser, call-control events, confirmation, snooze, quiet hours, bounded repetition, wellbeing/hydration events, gaze state and SOS priority.

firmware/lib/BuddyDisplay contains a portable RGB565 surface and the vendored FluxGarage RoboEyes library. The TFT adapter pushes its pixels with TFT_eSPI on the ESP32.

Both orientations have 76,800 pixels; the renderer uses a 153,600-byte RGB565 framebuffer, plus small renderer/library overhead. No downloaded movie, sprite sheet or stored animation-frame sequence is needed.

## Data paths

- Authenticated user/supervisor app → membership-scoped Care Hub → revisioned settings → paired ESP32 HubSync → validated LittleFS save → applied-revision acknowledgment.
- App-to-app calls → consent + authenticated signaling → WebRTC media. Buddy calls → accepted-only JPEG/PCM relay → browser or ESP32 adapters.
- Settings UI → Care Hub → persisted configuration → ESP32 sync → C++ schedule/renderer.
- Calendar UI → Care Hub dated schedule; the ESP32 independently schedules the same saved reminders.
- ESP32 I2S microphone → audio worker → Care Hub transcription → ESP32 C++ hear().
- Spoken responses → Care Hub TTS → streamed mono PCM to the ESP32 I2S speaker.
- Prescription photo → macOS Vision OCR → conservative editable draft → explicit review → settings saved to the care circle and synced to Buddy.
- ESP32 care events → Care Hub journal → calendar occurrence status and dashboard.
- ESP32 SOS/missed events → shared alerts. In-app calls are supported; no cellular/PSTN/SMS provider is included.

## Hardware limits

The firmware shows --% until a battery sensor is configured; it must not pretend to have measured battery state. SPI, I2S, camera and Wi-Fi behavior require physical testing on the assembled Buddy.

## Storage and clocks

Care Hub settings are written atomically with serialized writes. Firmware uses LittleFS settings and care-journal JSONL. Confirmed occurrences for the current day are restored before scheduling, preventing duplicate confirmed reminders on restart.

Schedules are persistent, but snooze deadlines and queued unconfirmed reminders are not yet reboot-persistent. The scheduler catches up at most 20 minutes late. It never recommends an extra or catch-up medication dose. A cold-boot ESP32 needs clock synchronization before scheduling. No DS3231 adapter has been implemented.

Both IANA (companion) and POSIX (firmware) timezone values are updated together by the app's supported timezone choices.

## Network, privacy and safety

Provider credentials are held only by the companion. Photos are processed locally and deleted from temporary storage. Reviewed prescription text and care events remain in local settings/history; protect the laptop and device.

Wake-name customization uses cloud transcription followed by local matching. Speech clips may contain words spoken before the wake phrase. Fully offline custom wake-word support is a separate hardware/model integration, not something this prototype claims.


The voice worker owns blocking recognition and playback so routine display/SOS input can continue; a priority SOS cancels queued speech. An in-flight HTTP operation still has a bounded timeout. Echo suppression and recognition quality need real acoustic testing.

The current Care Hub implements accounts, invitation permissions, device pairing, settings acknowledgments, bounded requests/rate limits and optional TLS. Account/circle data is stored in mode-0600 care-hub.json; credentials are hashed, but care records are not encrypted. Unauthenticated APIs are disabled. Public deployment, background notifications, recovery/revocation, storage protection and physical testing require further work; see [CARE_CIRCLES.md](CARE_CIRCLES.md).

## Third-party code

[FluxGarage RoboEyes](https://github.com/FluxGarage/RoboEyes), version 1.1.1, GPL-3.0-or-later. Full license and modification notice are vendored. The adapter supplies deterministic time, drawing primitives and a colored RGB565 palette.
