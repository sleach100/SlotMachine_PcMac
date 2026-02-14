#!/bin/bash
# ---------------------------------------------------------------
# create-macos-icon.sh
#
# Run this script ONCE on a Mac to generate SlotMachine.icns from
# the existing UpdatedSlotMachineIcon.png source image.
#
# Requirements:
#   - macOS (uses the built-in sips and iconutil commands)
#   - A square PNG source image at least 1024x1024 px
#
# Usage (from the project root):
#   chmod +x Resources/create-macos-icon.sh
#   Resources/create-macos-icon.sh
#
# Output:
#   Resources/SlotMachine.icns  — ready to reference in Projucer
# ---------------------------------------------------------------

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_PNG="$SCRIPT_DIR/UpdatedSlotMachineIcon.png"
ICONSET_DIR="$SCRIPT_DIR/AppIcon.iconset"
OUTPUT_ICNS="$SCRIPT_DIR/SlotMachine.icns"

# Verify source exists
if [ ! -f "$SOURCE_PNG" ]; then
    echo "ERROR: Source PNG not found at $SOURCE_PNG"
    exit 1
fi

echo "Source: $SOURCE_PNG"
echo "Output: $OUTPUT_ICNS"

# Clean and recreate the iconset directory
rm -rf "$ICONSET_DIR"
mkdir -p "$ICONSET_DIR"

# Generate all required icon sizes using sips (macOS built-in)
sips -z 16   16   "$SOURCE_PNG" --out "$ICONSET_DIR/icon_16x16.png"       > /dev/null
sips -z 32   32   "$SOURCE_PNG" --out "$ICONSET_DIR/icon_16x16@2x.png"    > /dev/null
sips -z 32   32   "$SOURCE_PNG" --out "$ICONSET_DIR/icon_32x32.png"       > /dev/null
sips -z 64   64   "$SOURCE_PNG" --out "$ICONSET_DIR/icon_32x32@2x.png"    > /dev/null
sips -z 128  128  "$SOURCE_PNG" --out "$ICONSET_DIR/icon_128x128.png"     > /dev/null
sips -z 256  256  "$SOURCE_PNG" --out "$ICONSET_DIR/icon_128x128@2x.png"  > /dev/null
sips -z 256  256  "$SOURCE_PNG" --out "$ICONSET_DIR/icon_256x256.png"     > /dev/null
sips -z 512  512  "$SOURCE_PNG" --out "$ICONSET_DIR/icon_256x256@2x.png"  > /dev/null
sips -z 512  512  "$SOURCE_PNG" --out "$ICONSET_DIR/icon_512x512.png"     > /dev/null
sips -z 1024 1024 "$SOURCE_PNG" --out "$ICONSET_DIR/icon_512x512@2x.png"  > /dev/null

echo "Generated iconset with $(ls "$ICONSET_DIR" | wc -l | tr -d ' ') images."

# Convert to .icns
iconutil -c icns "$ICONSET_DIR" -o "$OUTPUT_ICNS"

echo "Done: $OUTPUT_ICNS"
echo ""
echo "Next step: In Projucer, open NewProject.jucer and set the macOS app icon"
echo "to: Resources/SlotMachine.icns"
