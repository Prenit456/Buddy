# Buddy hardware: September 23 assembly

The current code follows the wiring completed in the Hardware task. The user
reports that every component passed its individual test. The combined firmware
has not yet been exercised on the physical assembly.

Build memory/controller settings remain ESP32-S3 N16R8 (16 MB flash, 8 MB octal
PSRAM) and ILI9341. Confirm these against the tested module settings; photographs
alone do not prove the memory suffix or display controller.

| Part | Signal | GPIO / connection |
|---|---|---|
| TFT | MOSI / SCK / CS / DC / RESET | 39 / 38 / 41 / 40 / 42 |
| TFT | MISO and all touch pins | Disconnected |
| TFT | LED | Direct 3V3; no software brightness control |
| MAX98357A | BCLK / LRC / DIN | 18 / 21 / 2 |
| I2S microphone | SCK / WS / SD | 18 / 21 / 1 |
| I2S microphone | L/R | GND; left slot |
| OV7670 | D0–D7 | 4 / 5 / 6 / 7 / 8 / 9 / 10 / 11 |
| OV7670 | XLK / PLK / VS / HS | 12 / 13 / 14 / 15 |
| OV7670 | SDA / SCL | 16 / 17 |
| OV7670 | RET / PWDN | 3V3 / GND |
| SOS button / buzzer / battery ADC | GPIO | Disabled (-1), not present in the supplied wiring |

Camera and microphone are powered from 3V3. The TFT's tested power connection
must be retained; no 5 V TFT compatibility is inferred. The amplifier uses its
regulated 5 V supply and common GND. Both speaker wires terminate at amplifier
outputs, never GND. See the earlier wiring discussion for the power arrangement.

The clock pins are deliberately shared. One master I2S controller handles TX
and RX using 32-bit stereo slots. Mono speech is duplicated into both output
slots, so the amplifier's default mix reproduces it correctly. Microphone input
uses the left slot only. During TTS the microphone data is discarded; after
playback the clock returns to 16 kHz and buffered echo is cleared.

OV7670 does not supply compressed JPEG. The camera driver captures RGB565 at
320×240 and converts it to JPEG for the existing Care Hub media endpoint. The
camera's XCLK is on LEDC channel 0; no PWM backlight or buzzer is attached to a
camera/audio pin. HardwareValidation.h rejects pin collisions at compile time.

No physical SOS button is assigned. If one is installed later, provide its actual
GPIO and set BUDDY_SOS_PIN. GPIO4 cannot be used: it is camera D0. Once configured,
a 1.2-second hold starts/cancels SOS and a short press ends an active call.
Without the button, voice SOS needs working cloud transcription, and app controls
remain available when the hub is reachable.

Battery sensing is disabled and the display shows --%. There is no RTC/battery
circuit in these photos. An offline cold boot needs a valid clock before timed
reminders can run. Person tracking and fall detection are not implemented.

See [FLASHING.md](FLASHING.md) for the complete firmware build, upload, Wi-Fi
setup, pairing, API configuration, and verification instructions.
