# Buddy firmware for the September 23 wiring

This is the complete PlatformIO/Arduino project in `firmware/`, not a single
sketch to paste into Arduino IDE. It contains the existing Buddy reminder core,
RoboEyes renderer, care-circle pairing/sync, voice commands, conversation, family
messages, and consent-controlled audio/video call adapter.

The build is configured for **ESP32-S3 N16R8 (16 MB flash and 8 MB octal
PSRAM)**, which the user confirmed. It currently selects an **ILI9341 TFT** and
an **INMP441-compatible I2S microphone with L/R grounded**. The microphone
appears to match the user's tests; the TFT controller has not yet been confirmed.
Check the display driver named in your successful TFT test before flashing.

## Build and upload on this Mac

From the project directory:

```sh
cd /Users/preetib/Desktop/ideathon
bash scripts/firmware.sh build
```

Connect the ESP32's **COM/UART** USB connector, then list serial ports:

```sh
ls /dev/cu.*
```

Use the ESP32 port that appears. Replace `/dev/cu.YOUR_PORT` below with that
actual port; it is not a literal device name.

```sh
bash scripts/firmware.sh upload /dev/cu.YOUR_PORT
bash scripts/firmware.sh monitor /dev/cu.YOUR_PORT
```

Press RESET after opening the monitor if you missed the boot messages. This
build logs through UART0 at 115200 baud; the board's COM connector carries it.
The USB-OTG connector is not configured as a runtime serial console in this build.
If upload cannot enter the bootloader automatically, hold BOOT, tap RESET,
release BOOT, then retry upload.
If macOS reports `Could not configure port: Device not configured` while
writing, treat the image as incomplete. Reconnect a reliable data cable
directly to the Mac, use the slower configured 115200-baud upload, and retry
until PlatformIO prints both `Hash of data verified` for the application and
`SUCCESS`. Do not judge Buddy's runtime behavior after a partial upload.

The script uses installed PlatformIO, or the already cached compiler in
`/private/tmp/buddy-pio-venv` and `/private/tmp/buddy-pio-music` on this Mac. If that
temporary cache has been cleared, install PlatformIO Core or use the PlatformIO
extension in VS Code. Open the **firmware folder**, select `buddy-s3-camera`, and
Build/Upload. Arduino ESP32 **2.0.17** is pinned by the PlatformIO platform version;
the legacy I2S adapter is not an Arduino ESP32 3.x sketch.

Do not upload the web filesystem: it is unnecessary for care-circle mode and
would replace saved device settings/history. A blank filesystem is initialized
on first boot. The compiled application is
`firmware/.pio/build/buddy-s3-camera/firmware.bin`; PlatformIO also uploads the
required bootloader and partition table at their correct offsets.

## Start the Care Hub

On the computer, in a separate terminal:

```sh
cd /Users/preetib/Desktop/ideathon
BUDDY_HOST=0.0.0.0 npm start
```

Keep this process running on the same trusted Wi-Fi network as Buddy. Open
http://localhost:4173 on this computer for the existing app. Provider keys are
already configured in `.env.local`: ElevenLabs transcription and primary TTS,
Google Gemini TTS fallback, and NVIDIA conversation. No provider key is compiled into the
ESP32; the Care Hub reads them from its local `.env.local` file.
The Care Hub supports only physical ESP32 devices. The old demo setting is
ignored. In the signed-in site, **My Buddy** must show a `Paired ESP32`, its latest contact, and
a device revision matching the saved revision before a website change is
considered applied to hardware.

**Service status:** the September 23 live checks passed for ElevenLabs TTS/STT,
Google TTS fallback and the former OpenRouter conversation path. The three
extra ElevenLabs keys returned `missing_permissions` for TTS; enable Text to
Speech in their settings before relying on them. On September 24, the NVIDIA
chat path answered through an isolated authenticated device route; it still
needs a real physical-device check with replacement NVIDIA keys. Built-in commands such as time, reminders
and SOS do not use the conversation model, but spoken input still requires
working transcription. See `VERIFICATION.md` for exact test results.

For the ESP32, use the computer's actual Wi-Fi IPv4 address followed by `:4173`.
Find it in macOS **System Settings → Wi-Fi → Details → TCP/IP**. A URL such as
`http://192.168.1.10:4173` is an example, not an assigned address. `localhost`
on the ESP32 refers to the ESP32, not the computer. If macOS asks whether Node
can accept incoming local-network connections, allow it for this connection.

If an older Care Hub is already running on port 4173, stop that terminal's
process with Ctrl-C, then start the command above to load the updated code.

## Connect and pair

1. On first boot, join **Buddy-Setup**, password **carebuddy**, from a phone or
   laptop. Open `http://192.168.4.1` if the captive portal does not appear.
2. Select the 2.4 GHz Wi-Fi network and enter its password. Enter the reachable
   **Care Hub URL**, then save. The portal closes after three minutes if unused;
   reset the device to try again.
3. Serial Monitor prints the detected flash/PSRAM size, camera PID, audio status,
   local setup PIN, local IP, and a ten-minute app pairing code.
4. Sign in as the user in the existing Buddy app. In **My Buddy**, use the
   physical device's pairing code.
5. Once paired, saved settings/reminders sync to the ESP32. The speaker sounds
   a boot chime even before API configuration. After pairing, try
   “Hey Buddy, what is the time?” (or the configured call name).

On the website, use **Talk with Buddy** for typed conversation. For a carer
video call, invite a supervisor in **My people**, have them join the same care
circle, then let them press **Video call Buddy** on Today or My Buddy. Buddy
must be online and report camera/audio ready. The call rings first; Buddy's
user accepts it before any media is exchanged. A distant carer needs secure
access to the Care Hub (for example, trusted HTTPS or a private VPN). Buddy
video uses the Care Hub's authenticated JPEG/PCM relay, not browser-to-browser
WebRTC; TURN is relevant only for app-to-app browser calls across networks.

If Wi-Fi was already saved and no portal appears, visit the IP printed in Serial
Monitor. Username is **buddy**; password is the per-boot setup PIN printed there.
Set the Care Hub URL on that page. Its authenticated `/api/status` endpoint shows
audio/camera availability, free memory, and whether a physical SOS button exists.

## Wiring used by this build

| Peripheral | Signals → GPIO |
|---|---|
| TFT | MOSI 39, SCK 38, CS 41, DC 40, RESET 42; MISO disconnected |
| Microphone | SCK 18, WS 21, SD 1; L/R grounded |
| MAX98357A | BCLK 18, LRC 21, DIN 2 |
| OV7670 data | D0–D7 → 4, 5, 6, 7, 8, 9, 10, 11 |
| OV7670 timing/control | XLK 12, PLK 13, VS 14, HS 15, SDA 16, SCL 17 |
| Camera RET / PWDN | 3V3 / GND, respectively; neither is a GPIO |
| TFT LED | Direct 3V3; brightness is fixed with this wiring |
| Touch, microSD, buzzer, battery sensing | Unused |
| Physical SOS button | Disabled until its actual GPIO is supplied |

The microphone and amplifier use one I2S controller with shared clocks, 32-bit
stereo slots, and separate data lines. Listening is 16 kHz; during speech the
shared bus switches to the WAV's sample rate, then restores microphone timing.
Microphone audio is ignored during Buddy's own speech. This is not acoustic
echo cancellation. Microphone shift and speech threshold can be calibrated in
`HardwareConfig.h` if quiet speech is missed or room noise triggers uploads.

OV7670 captures 320×240 RGB565. The ESP32 converts frames to JPEG for the existing
Care Hub relay. It captures call frames only during accepted video/camera calls;
startup performs a sensor initialization check. There is no person tracking or
fall detection in the agreed implementation.

## What depends on connectivity

Saved schedules and screen animation run locally; offline reminders use speaker
chimes. Spoken replies, wake-phrase transcription, conversation, remote sync and
calls need the Care Hub, and cloud voice/conversation need internet. Wake phrases
are checked after short speech clips are transcribed by ElevenLabs; this is not
offline keyword detection. During calls, cloud wake transcription pauses.

With no SOS button installed, start/cancel SOS by voice when speech service is
available, or through the app. During calls, use the app to hang up; voice hang-up
during an active call is not implemented. Adding the physical button later needs
its GPIO assigned in `HardwareConfig.h`; GPIO4 is already camera D0.

Browser microphone/camera use requires localhost or trusted HTTPS. A phone
opening the hub over plain LAN HTTP can manage settings, but cannot be assumed
to support media calls. See `CARE_CIRCLES.md` for HTTPS, certificate configuration
on the ESP32, and cross-network calling. The HTTP media relay is a prototype;
audio latency and camera frame rate require an assembled-hardware test.

## Verification

```sh
npm test
npm run test:integration
```

The firmware build and tests verify compilation, audio sample/WAV conversion,
reminder logic, rendering, pairing, settings sync and the call protocol. They do
not verify the assembled board's electrical behavior, microphone sensitivity,
speaker acoustics, or end-to-end call quality.

An optional `npm run test:providers` uses small amounts of provider credit for
synthetic speech/transcription/conversation and a speech-fallback check. It uses
temporary local accounts and does not change real care-circle data or keys.
