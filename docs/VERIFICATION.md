# Verification and remaining work

## Verified in this workspace

- Native C++ compiles with C++17, -Wall, -Wextra and -pedantic.
- Core tests: dates/weekdays, multiple reminders, snooze, missed alerts, wake-name changes/follow-up expiry, SOS priority, restored confirmations, quiet hours, repeat limits and hydration confirmation.
- Renderer tests: deterministic RGB565 pixels for all seven modes in both native orientations.
- JavaScript syntax and conservative prescription/date validation tests.
- Isolated HTTP integration tests: actual C++ framebuffer dimensions/payload, wake-name commands, SOS protection, reviewed prescription date windows, saved settings and journal across restart.
- Synthetic printed prescription image recognized by local macOS Vision; two medicine-like lines preserved, ambiguous frequency left unfilled.
- Live ElevenLabs TTS produced PCM WAV, then STT correctly recognized the synthetic phrase “Hey Buddy, what is the time?”.
- Browser visual inspection: device frame/top SOS, calendar and prescription-review form.
- Care-circle integration tests: role/circle isolation; hashed credentials; single-use invitations; edit permissions; device pairing; optimistic versions and applied revisions; durable command/event deduplication; task/alert claims; check-in replies; accepted-only camera media, microphone prohibition, privacy revocation; bidirectional PCM relay; device voice-call commands; accepted-only WebRTC signaling; restart persistence.
- Browser checks with **isolated fictional accounts**: responsive phone and 1280-pixel laptop layouts, user/supervisor login and dashboards, embedded C++ calendar/device lab, Buddy-originated incoming call on the supervisor side, and decline without microphone access.
- Earlier PlatformIO compilation succeeded for `buddy-s3`, `buddy-s3-voice`, and `buddy-s3-camera` using Espressif32 6.10.0 / Arduino ESP32 2.0.17. The September 23 wired camera build was rebuilt successfully: 1,278,581 bytes flash and 66,172 bytes static RAM. These numbers exclude runtime heap/PSRAM buffers and are not a measurement of total memory usage.
- September 23 wiring is configured in the firmware: TFT without touch/readback, shared-clock microphone/amplifier, and OV7670 RGB565 capture with software JPEG conversion. Compile-time checks reject pin collisions. The physical SOS button and backlight PWM are disabled because no corresponding GPIO was supplied.
- Native audio tests cover signed microphone conversion, speaker volume/stereo slots, fragmented WAV reads, metadata chunks and rejection of unsupported/malformed audio.
- Latest `npm test` passed; all 17 isolated HTTP/care-circle integration tests passed after the firmware handoff changes.
- Final website pass: the typed **Talk with Buddy** screen rendered in a browser with fictional accounts. Hardware calling still requires physical end-to-end verification on the ESP32.
- Final provider round trip: ElevenLabs primary TTS, ElevenLabs speech-to-text, OpenRouter conversation, and Google TTS after deliberately invalid ElevenLabs keys all passed through authenticated device routes. The complete `buddy-s3-camera` PlatformIO build and all 17 isolated integration tests passed. This does not replace a physical video-call quality test.

## Live provider checks — September 23, 2026

The authenticated device endpoints were exercised using temporary fictional
accounts and synthetic speech, without changing saved provider keys or real care
data:

| Service | Result |
|---|---|
| Google Gemini TTS fallback | Passed; returned PCM WAV using `gemini-3.1-flash-tts-preview` after deliberately invalid ElevenLabs keys |
| ElevenLabs transcription | Passed; recognized the synthetic wake phrase after conversion to the ESP32's 16 kHz WAV format |
| ElevenLabs primary TTS | Passed; returned PCM WAV through the authenticated device API |
| NVIDIA conversation (previous configuration) | Generation timed out at both the hub's 45-second timeout and a separate 90-second streaming diagnostic |
| OpenRouter ABC key | Authenticated and returned a short reply through `openrouter/free`; configured as primary |
| OpenRouter PRENIT key | Authenticated and returned a short reply through `openrouter/free`; configured as backup |
| OpenRouter ARDUINO key | Authenticated after correcting the pasted capital `S`, but the free route returned an empty reply and a specific-model retry was rate limited; not configured |
| OpenRouter HELLO key | Returned HTTP 401; not configured |
| Authenticated Buddy device conversation | **Passed** using isolated test accounts and a synthetic prompt after the OpenRouter switch |

The previous NVIDIA model-list endpoint returned HTTP 200 and included the
configured `openai/gpt-oss-20b` model, but generation did not complete. The Care
Hub now uses OpenRouter for conversation; its local `.env.local` contains the two
keys that produced replies, in primary/backup order, and no NVIDIA conversation
key. Both keys report free-tier access and belong to different OpenRouter accounts.
Requests use the free model route. The free router may pick different models
or hit rate limits, so one successful synthetic exchange does not prove sustained
availability or spoken conversation on the physical assembly. Local reminder and
time commands do not depend on OpenRouter.

No real prescription was imported and test settings were isolated from the saved care configuration.

## Not yet verified

- Flashing and operation on physical hardware. Successful peripheral-driver compilation does not prove pin, electrical or runtime compatibility.
- Physical SPI colors/timing, I2S pin/channel/level compatibility, room microphone recognition, acoustic echo and speaker playback.
- Real-user hands-free operation: the live test used synthesized speech, not a microphone recording.
- Handwritten prescription accuracy or any clinical validation.
- End-to-end microphone/webcam media quality in the browser or physical Buddy. Call state/signaling and synthetic PCM/JPEG exchanges were tested; no personal camera/microphone was opened during UI verification.
- Real caregiver notification delivery and response. In-app calls/alerts require an open app and connectivity; no background push, cellular calls or SMS are included.
- Offline custom wake-word models, automatic fall detection, camera/ToF person tracking, RTC cold-boot support, and battery ADC calibration are not implemented.

The user confirmed the ESP32 module is N16R8. The current build selects ILI9341
and an INMP441-compatible microphone; the microphone appears to match the user's
tests, but the tested TFT driver still needs confirmation. Camera pins
are now configured. No provider key is embedded in the app or firmware, and the
combined firmware has not been flashed or physically exercised by this agent.
