#!/usr/bin/env bash
set -euo pipefail

buddy_project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$buddy_project_dir/firmware"

if command -v pio >/dev/null 2>&1; then
  buddy_pio="$(command -v pio)"
elif [[ -x "${HOME}/.platformio/penv/bin/pio" ]]; then
  # PlatformIO installed by the VS Code extension is often absent from PATH.
  buddy_pio="${HOME}/.platformio/penv/bin/pio"
elif [[ -x /private/tmp/buddy-pio-venv/bin/pio && -d /private/tmp/buddy-pio-music ]]; then
  # Reuse the compiler already installed for this project on this Mac.
  buddy_pio=/private/tmp/buddy-pio-venv/bin/pio
  export PLATFORMIO_CORE_DIR=/private/tmp/buddy-pio-music
else
  printf '%s\n' 'PlatformIO Core is required. Open the firmware folder in VS Code with PlatformIO, or install PlatformIO Core.' >&2
  exit 1
fi
export PLATFORMIO_SETTING_ENABLE_TELEMETRY=no

case "${1:-build}" in
  build)
    exec "$buddy_pio" run -e buddy-s3-camera
    ;;
  upload)
    if [[ -z "${2:-}" ]]; then
      printf '%s\n' 'Supply the ESP32 serial port: bash scripts/firmware.sh upload /dev/cu.YOUR_PORT' >&2
      exit 2
    fi
    exec "$buddy_pio" run -e buddy-s3-camera --target upload --upload-port "$2"
    ;;
  monitor)
    if [[ -z "${2:-}" ]]; then
      printf '%s\n' 'Supply the ESP32 serial port: bash scripts/firmware.sh monitor /dev/cu.YOUR_PORT' >&2
      exit 2
    fi
    exec "$buddy_pio" device monitor --port "$2" --baud 115200
    ;;
  *)
    printf '%s\n' 'Usage: bash scripts/firmware.sh build|upload|monitor [serial-port]' >&2
    exit 2
    ;;
esac
