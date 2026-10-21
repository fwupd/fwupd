#!/bin/sh

exec 0>/dev/null
exec 2>&1

TMPDIR="$(mktemp -d)"
trap 'rm -rf -- "$TMPDIR"' EXIT

export NO_COLOR=1

# keep everything fwupd reads and writes in the temporary directory, and use
# the devicetree in place of /sys/firmware/fdt to fake a VBE system
export FWUPD_SYSFSFWDIR=${TMPDIR}/sys
export FWUPD_LOCALSTATEDIR=${TMPDIR}/var
export FWUPD_LOCKDIR=${TMPDIR}/lock
export CACHE_DIRECTORY=${TMPDIR}/cache
export CONFIGURATION_DIRECTORY=${TMPDIR}/etc
mkdir -p "${FWUPD_SYSFSFWDIR}" "${FWUPD_LOCALSTATEDIR}" "${FWUPD_LOCKDIR}" \
    "${CACHE_DIRECTORY}" "${CONFIGURATION_DIRECTORY}"

STORAGE=${TMPDIR}/storage.img
GUID=5ba31c9e-b9e6-5432-8f97-ca6c4c91a63b # VBE\COMPATIBLE_vendor-board

error() {
    cat fwupdtool.txt
    echo " ● exit code was ${1} and expected ${2}"
    exit 1
}

expect_rc() {
    rc=$?
    expected=$1

    [ "$expected" -eq "$rc" ] || error "$rc" "$expected"
}

run() {
    cmd="fwupdtool -v --plugins vbe $*"
    echo " ● cmd: $cmd" >fwupdtool.txt
    $cmd 1>>fwupdtool.txt 2>&1
}

# check that the storage holds $2 at offset $1, i.e. firmware with the first
# 0x10 bytes skipped, with nothing written before or after it
expect_storage() {
    dd if="${STORAGE}" of="${TMPDIR}/got.bin" bs=1 skip=$(($1)) count=${#2} 2>/dev/null
    printf '%s' "$2" | cmp -s - "${TMPDIR}/got.bin" ||
        error "storage at $1 is $(cat "${TMPDIR}/got.bin")" "$2"
    dd if="${STORAGE}" of="${TMPDIR}/before.bin" bs=1 count=$(($1)) 2>/dev/null
    head -c $(($1)) /dev/zero | cmp -s - "${TMPDIR}/before.bin" ||
        error "storage changed before $1" "no change"
    end=$(($1 + ${#2}))
    tail -c +$((end + 1)) "${STORAGE}" >"${TMPDIR}/after.bin"
    head -c $(($(wc -c <"${STORAGE}") - end)) /dev/zero |
        cmp -s - "${TMPDIR}/after.bin" ||
        error "storage changed after the update" "no change"
}

# make a cab for a FIT, with the given version
build_cab() {
    cat >"${TMPDIR}/vbe.metainfo.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<component type="firmware">
  <id>com.Vbe.Test.firmware</id>
  <name>VBE Test</name>
  <summary>Firmware for testing VBE</summary>
  <provides>
    <firmware type="flashed">${GUID}</firmware>
  </provides>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>LGPL-2.1-or-later</project_license>
  <developer_name>VBE</developer_name>
  <releases>
    <release version="$2" date="2026-10-21">
      <checksum filename="firmware.fit" target="content"/>
    </release>
  </releases>
</component>
EOF
    cp "$1" "${TMPDIR}/firmware.fit"
    run build-cabinet "${TMPDIR}/vbe.cab" "${TMPDIR}/firmware.fit" \
        "${TMPDIR}/vbe.metainfo.xml" --force
    expect_rc 0
}

# skip if the plugin is not built, e.g. on a system other than Linux
run get-plugins
expect_rc 0
grep -q '^vbe:' fwupdtool.txt || {
    echo " ● vbe plugin not available"
    exit 77
}

# the devicetree as the firmware provides it when it boots the given version,
# with the firmware area at 0x1000
boot() {
    cat >"${TMPDIR}/fdt.builder.xml" <<EOF
<firmware gtype="FuFdtFirmware">
  <firmware gtype="FuFdtImage">
    <metadata key="compatible" format="str">vendor,board</metadata>
    <firmware gtype="FuFdtImage">
      <id>chosen</id>
      <firmware gtype="FuFdtImage">
        <id>fwupd</id>
        <metadata key="compatible" format="str">vendor,board</metadata>
        <firmware gtype="FuFdtImage">
          <id>firmware</id>
          <metadata key="compatible" format="str">fwupd,vbe-simple</metadata>
          <metadata key="cur-version" format="str">$1</metadata>
          <metadata key="storage" format="str">${STORAGE}</metadata>
          <metadata key="area-start" format="uint32">0x1000</metadata>
          <metadata key="area-size" format="uint32">0x3000</metadata>
          <metadata key="skip-offset" format="uint32">0x10</metadata>
        </firmware>
      </firmware>
    </firmware>
  </firmware>
</firmware>
EOF
    run firmware-build "${TMPDIR}/fdt.builder.xml" "${FWUPD_SYSFSFWDIR}/fdt"
    expect_rc 0
}

# fwupd only allows another update after a reboot, which it detects from the
# kernel's boot time, so start again with fresh state, booting the new version
reboot() {
    rm -rf "${FWUPD_LOCALSTATEDIR}"
    mkdir -p "${FWUPD_LOCALSTATEDIR}"
    boot "$1"
}

echo " ● Booting the firmware…"
boot 1.2.3
head -c 16384 /dev/zero >"${STORAGE}"

# ---
echo " ● Checking the vbe-simple device…"
run get-devices
expect_rc 0
grep -q "${GUID}" fwupdtool.txt || error "no ${GUID}" "the board's GUID"

# ---
echo " ● Building a FIT with a configuration for the board…"
cat >"${TMPDIR}/fit.builder.xml" <<EOF
<firmware gtype="FuFitFirmware">
  <firmware gtype="FuFdtImage">
    <metadata key="timestamp" format="uint32">0x629d4abd</metadata>
    <firmware gtype="FuFdtImage">
      <id>images</id>
      <firmware gtype="FuFdtImage">
        <id>firmware-1</id>
        <metadata key="type" format="str">firmware</metadata>
        <metadata key="description" format="str">test</metadata>
        <metadata key="data" format="data">MDEyMzQ1Njc4OWFiY2RlZjAxMjM0NTY3ODlhYmNkZWY=</metadata>
        <metadata key="store-offset" format="uint32">0x200</metadata>
      </firmware>
    </firmware>
    <firmware gtype="FuFdtImage">
      <id>configurations</id>
      <firmware gtype="FuFdtImage">
        <id>conf-1</id>
        <metadata key="compatible" format="strlist">vendor,board</metadata>
        <metadata key="version" format="str">1.2.4</metadata>
        <metadata key="firmware" format="strlist">firmware-1</metadata>
      </firmware>
    </firmware>
  </firmware>
</firmware>
EOF
run firmware-build "${TMPDIR}/fit.builder.xml" "${TMPDIR}/test.fit"
expect_rc 0
build_cab "${TMPDIR}/test.fit" 1.2.4

# ---
echo " ● Installing it…"
run install "${TMPDIR}/vbe.cab" ${GUID}
expect_rc 0
expect_storage 0x1210 0123456789abcdef

# ---
echo " ● Rebooting into it…"
reboot 1.2.4

# ---
echo " ● Building a FIT with external data and a configuration for any board…"
# this is built as a plain devicetree, since it is not a valid FIT until the data
# has been added
cat >"${TMPDIR}/fit-ext.builder.xml" <<EOF
<firmware gtype="FuFdtFirmware">
  <firmware gtype="FuFdtImage">
    <metadata key="timestamp" format="uint32">0x629d4abd</metadata>
    <firmware gtype="FuFdtImage">
      <id>images</id>
      <firmware gtype="FuFdtImage">
        <id>firmware-1</id>
        <metadata key="type" format="str">firmware</metadata>
        <metadata key="description" format="str">test</metadata>
        <metadata key="data-offset" format="uint32">0x0</metadata>
        <metadata key="data-size" format="uint32">0x20</metadata>
        <metadata key="store-offset" format="uint32">0x200</metadata>
      </firmware>
    </firmware>
    <firmware gtype="FuFdtImage">
      <id>configurations</id>
      <firmware gtype="FuFdtImage">
        <id>conf-1</id>
        <metadata key="version" format="str">1.2.5</metadata>
        <metadata key="firmware" format="strlist">firmware-1</metadata>
      </firmware>
    </firmware>
  </firmware>
</firmware>
EOF
run firmware-build "${TMPDIR}/fit-ext.builder.xml" "${TMPDIR}/test-ext.fit"
expect_rc 0

# the data follows the FIT, which is padded to 4 bytes, as 'mkimage -E' does
size=$(wc -c <"${TMPDIR}/test-ext.fit")
head -c $(((4 - size % 4) % 4)) /dev/zero >>"${TMPDIR}/test-ext.fit"
printf '0123456789ABCDEF0123456789ABCDEF' >>"${TMPDIR}/test-ext.fit"
build_cab "${TMPDIR}/test-ext.fit" 1.2.5

# ---
echo " ● Installing it…"
head -c 16384 /dev/zero >"${STORAGE}"
run install "${TMPDIR}/vbe.cab" ${GUID}
expect_rc 0
expect_storage 0x1210 0123456789ABCDEF
