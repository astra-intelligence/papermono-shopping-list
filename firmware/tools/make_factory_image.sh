#!/usr/bin/env bash
# Merge a PlatformIO build (bootloader + partition table + app) into one image that flashes at
# offset 0x0 - what browser-based flashers such as https://espressif.github.io/esptool-js/ expect.
#
# Usage:
#   pio run                                   # build first; this script doesn't
#   tools/make_factory_image.sh [--catalog path/to/catalog.json]
#
# Writes dist/papermono-shopping-list.factory.bin and prints its SHA-256. With --catalog, also
# copies the image next to that catalog.json and adds/replaces its entry, for a self-hosted
# esptool-js flasher that reads a {"firmware": [...]} catalog.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/.pio/build/papermono"
DIST_DIR="$PROJECT_DIR/dist"
IMAGE_NAME="papermono-shopping-list.factory.bin"

CATALOG=""
while [ $# -gt 0 ]; do
  case "$1" in
    --catalog) CATALOG="$2"; shift 2 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

for f in bootloader.bin partitions.bin firmware.bin; do
  if [ ! -f "$BUILD_DIR/$f" ]; then
    echo "error: $BUILD_DIR/$f not found - run 'pio run' first" >&2
    exit 1
  fi
done

# esptool v5+ (on PATH) spells it `merge-bin --flash-size`; the v4 copy PlatformIO bundles spells it
# `merge_bin --flash_size`. The bundled one must run under PlatformIO's own Python, which has pyserial.
if command -v esptool >/dev/null 2>&1; then
  ESPTOOL=(esptool); MERGE=merge-bin; FLASH_SIZE=--flash-size
elif BUNDLED=$(ls "$HOME"/.platformio/packages/tool-esptoolpy/esptool.py 2>/dev/null) && [ -n "$BUNDLED" ]; then
  PIO_PYTHON=python3
  if PIO_BIN=$(command -v pio) && [ -x "$(dirname "$PIO_BIN")/python3" ]; then
    PIO_PYTHON="$(dirname "$PIO_BIN")/python3"
  fi
  ESPTOOL=("$PIO_PYTHON" "$BUNDLED"); MERGE=merge_bin; FLASH_SIZE=--flash_size
else
  echo "error: no esptool found - install it (pip install esptool) or run 'pio run' once" >&2
  exit 1
fi

mkdir -p "$DIST_DIR"
IMAGE="$DIST_DIR/$IMAGE_NAME"
# No otadata/boot_app0 segment: partitions_16mb.csv has a single factory app and no OTA slots.
"${ESPTOOL[@]}" --chip esp32s3 "$MERGE" -o "$IMAGE" "$FLASH_SIZE" 16MB \
  0x0 "$BUILD_DIR/bootloader.bin" \
  0x8000 "$BUILD_DIR/partitions.bin" \
  0x10000 "$BUILD_DIR/firmware.bin" >/dev/null

SHA256=$(sha256sum "$IMAGE" | cut -d' ' -f1)
echo "$IMAGE"
echo "sha256 $SHA256"

if [ -n "$CATALOG" ]; then
  cp "$IMAGE" "$(dirname "$CATALOG")/$IMAGE_NAME"
  python3 - "$CATALOG" "$IMAGE_NAME" "$SHA256" "$(date +%Y-%m-%d)" <<'PY'
import json, sys
from pathlib import Path

path, image, sha256, version = Path(sys.argv[1]), *sys.argv[2:]
catalog = json.loads(path.read_text()) if path.exists() else {"firmware": []}
entries = [e for e in catalog.setdefault("firmware", []) if e.get("id") != "papermono-shopping-list"]
entries.append({
    "id": "papermono-shopping-list",
    "name": "PaperMono Shopping List",
    "version": version,
    "description": "E-paper shopping list that syncs with its server over Wi-Fi.",
    "file": image,
    "offset": 0,
    "sha256": sha256,
})
catalog["firmware"] = entries
path.write_text(json.dumps(catalog, indent=2) + "\n")
print(f"registered in {path}")
PY
fi
