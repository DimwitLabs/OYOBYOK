#!/usr/bin/env bash
# Flash a built image to the device: bootloader, partition table, app and OTA data.
# Usage: ./flash.sh [/dev/cu.usbmodemXXXX]
set -euo pipefail
cd "$(dirname "$0")"

if ! command -v esptool.py >/dev/null 2>&1 || ! command -v python >/dev/null 2>&1; then
    if [ -n "${IDF_PATH:-}" ] && [ -f "$IDF_PATH/export.sh" ]; then
        # shellcheck disable=SC1091
        source "$IDF_PATH/export.sh" >/dev/null 2>&1
    elif [ -f "$HOME/esp/esp-idf/export.sh" ]; then
        # shellcheck disable=SC1091
        source "$HOME/esp/esp-idf/export.sh" >/dev/null 2>&1
    fi
fi

PORT="${1:-}"
if [ -z "$PORT" ]; then
    for p in /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/ttyACM* /dev/ttyUSB*; do
        [ -e "$p" ] && { PORT="$p"; break; }
    done
fi
[ -n "$PORT" ] || { echo "No serial port found. Plug the device in, or pass the port as the first argument."; exit 1; }
echo ">> port: $PORT"

for f in build/bootloader/bootloader.bin build/partition_table/partition-table.bin build/oyobyok.bin build/ota_data_initial.bin; do
    [ -f "$f" ] || { echo "missing $f; run 'idf.py build' first"; exit 1; }
done

esptool.py --chip esp32s3 -p "$PORT" -b 460800 \
    --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
    0x0      build/bootloader/bootloader.bin \
    0x8000   build/partition_table/partition-table.bin \
    0x10000  build/oyobyok.bin \
    0x910000 build/ota_data_initial.bin

echo ">> done; the device resets into OYOBYOK"
