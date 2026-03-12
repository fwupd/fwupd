#!/bin/bash
# Build firmware cabinet file for fwupd
# Usage: ./build-cab.sh firmware.bin 1.0.0

set -e

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <firmware.bin> <version>"
    echo "Example: $0 firmware.bin 1.0.0"
    exit 1
fi

FIRMWARE_FILE="$1"
VERSION="$2"
METAINFO_FILE="com.lxsemicon.touchpad.metainfo.xml"
OUTPUT_CAB="lxs-touchpad-${VERSION}.cab"

# Check firmware file exists
if [ ! -f "$FIRMWARE_FILE" ]; then
    echo "Error: Firmware file '$FIRMWARE_FILE' not found"
    exit 1
fi

# Check firmware size
FILESIZE=$(stat -c%s "$FIRMWARE_FILE" 2>/dev/null || stat -f%z "$FIRMWARE_FILE")
if [ "$FILESIZE" -ne 118784 ] && [ "$FILESIZE" -ne 131072 ]; then
    echo "Warning: Firmware size is $FILESIZE bytes"
    echo "Expected 118784 (116KB app) or 131072 (128KB full)"
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Check metainfo exists
if [ ! -f "$METAINFO_FILE" ]; then
    echo "Error: Metainfo file '$METAINFO_FILE' not found"
    exit 1
fi

# Update version in metainfo
sed -i.bak "s/<release version=\"[^\"]*\"/<release version=\"$VERSION\"/" "$METAINFO_FILE"

# Generate firmware.bin checksum
CHECKSUM=$(sha256sum "$FIRMWARE_FILE" | cut -d' ' -f1)
echo "Firmware checksum: $CHECKSUM"

# Create temporary directory
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# Copy files to temp dir
cp "$FIRMWARE_FILE" "$TMPDIR/firmware.bin"
cp "$METAINFO_FILE" "$TMPDIR/"

# Build cabinet file
pushd "$TMPDIR" > /dev/null
gcab --create "../$OUTPUT_CAB" firmware.bin "$METAINFO_FILE"
popd > /dev/null

# Restore original metainfo
mv "${METAINFO_FILE}.bak" "$METAINFO_FILE"

echo "✓ Created $OUTPUT_CAB"
echo ""
echo "To install:"
echo "  sudo fwupdmgr install $OUTPUT_CAB"
echo ""
echo "To test locally:"
echo "  fwupdmgr --verbose install $OUTPUT_CAB"
