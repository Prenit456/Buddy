# Buddy direct firmware

This is a separate PlatformIO project for the ESP32-S3 N16R8. Open **this
`buddy-direct` folder** in VS Code, not the older `firmware` folder. The entry
point is [`src/main.cpp`](src/main.cpp); the other C++ files in `src` and `lib`
are required parts of the same full Buddy program. There are no microphone or
speaker test sketches in this project, and `platformio.ini` names exactly the
seven C++ source files to compile.

To upload, connect Buddy by USB, then use PlatformIO's **Upload** action in
this project. The only build environment is `buddy-direct`. From a terminal,
the equivalent commands are:

```sh
cd /Users/preetib/Desktop/ideathon/buddy-direct
/Users/preetib/.platformio/penv/bin/pio run -e buddy-direct
/Users/preetib/.platformio/penv/bin/pio run -e buddy-direct -t upload
```

This project has the public Buddy backend URL configured by default. It does
**not** put AI or voice API keys on the device; those remain on the backend.
If the device has Wi-Fi but is not paired, its screen shows the pairing code.
Enter that code on the deployed Buddy website. If the backend's URL ever
changes, update `include/HubConfig.h` and upload again, or use the Wi-Fi setup
portal's **Buddy app URL** field.

The screen uses a text band below the eyes for recognized speech. Voice and
website settings require a connected Wi-Fi network, a reachable backend, and
a claimed device. A successful upload alone does not prove those are working;
check the pairing state on the screen and then test with a short sentence
starting with **Hey**.

The TFT touch pins are not used, and the display's SDO/MISO is intentionally
disconnected.
