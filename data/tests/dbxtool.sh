#!/bin/sh

exec 0>/dev/null
exec 2>&1

TMPDIR="$(mktemp -d)"
trap 'rm -rf -- "$TMPDIR"' EXIT

export NO_COLOR=1
export CACHE_DIRECTORY=${TMPDIR}/cache
export STATE_DIRECTORY=${TMPDIR}/state
export FWUPD_SYSFSFWDIR=${TMPDIR}/sys

# use these to fake a UEFI system
mkdir -p ${CACHE_DIRECTORY}
mkdir -p ${STATE_DIRECTORY}
mkdir -p ${FWUPD_SYSFSFWDIR}/efi/efivars

# GUID of the EFI_IMAGE_SECURITY_DATABASE
DBX_GUID=d719b2cb-3d3a-4596-a3bc-dad00e67656f

error() {
    cat dbxtool.txt
    echo " ● Exit code was ${1} and expected ${2}"
    exit 1
}

expect_rc() {
    rc=$?
    expected=$1

    [ "$expected" -eq "$rc" ] || error "$rc" "$expected"
}

run() {
    cmd="dbxtool -v $*"
    echo " ● cmd: $cmd" >dbxtool.txt
    $cmd 1>>dbxtool.txt 2>&1
}

# build an EFI_SIGNATURE_LIST to use as the installed dbx
cat >${TMPDIR}/dbx.builder.xml <<EOF
<firmware gtype="FuEfiSignatureList">
  <firmware gtype="FuEfiSignature">
    <kind>sha256</kind>
    <owner>77fa9abd-0359-4d32-bd60-28f4e78f784b</owner>
    <checksum>418ad44c79e3fddd6a0574b24fcf0fb8fee4b3ff2be635d21a5c0852bdea635c</checksum>
  </firmware>
</firmware>
EOF
fwupdtool firmware-build ${TMPDIR}/dbx.builder.xml ${TMPDIR}/dbx.bin >/dev/null 2>&1

# ...and a different one to use as an update
cat >${TMPDIR}/update.builder.xml <<EOF
<firmware gtype="FuEfiSignatureList">
  <firmware gtype="FuEfiSignature">
    <kind>sha256</kind>
    <owner>77fa9abd-0359-4d32-bd60-28f4e78f784b</owner>
    <checksum>0000000000000000000000000000000000000000000000000000000000000000</checksum>
  </firmware>
</firmware>
EOF
fwupdtool firmware-build ${TMPDIR}/update.builder.xml ${TMPDIR}/update.bin >/dev/null 2>&1

# seed the system dbx efivar: 4 bytes of attributes (NV|BS|RT) then the data
printf '\007\000\000\000' >${FWUPD_SYSFSFWDIR}/efi/efivars/dbx-${DBX_GUID}
cat ${TMPDIR}/dbx.bin >>${FWUPD_SYSFSFWDIR}/efi/efivars/dbx-${DBX_GUID}

# ---
echo " ● No action specified (should fail)…"
run
expect_rc 1

# ---
echo " ● Listing the system dbx…"
run --list
expect_rc 0

# ---
echo " ● Showing the system dbx version…"
run --version
expect_rc 0

# ---
echo " ● Listing a local dbx file…"
run --list --dbx ${TMPDIR}/dbx.bin
expect_rc 0

# ---
echo " ● Showing a local dbx file version…"
run --version --dbx ${TMPDIR}/dbx.bin
expect_rc 0

# ---
echo " ● Listing a missing local dbx file (should fail)…"
run --list --dbx ${TMPDIR}/does-not-exist.bin
expect_rc 1

# ---
echo " ● Applying with no filename (should fail)…"
run --apply
expect_rc 1

# ---
echo " ● Applying an already-applied dbx (should fail)…"
run --apply --dbx ${TMPDIR}/dbx.bin
expect_rc 1

# ---
echo " ● Applying a dbx update…"
run --apply --force --dbx ${TMPDIR}/update.bin
expect_rc 0
