#!/bin/sh

exec 0>/dev/null
exec 2>&1

TMPDIR="$(mktemp -d)"
trap 'rm -rf -- "$TMPDIR"' EXIT

export NO_COLOR=1
export FWUPD_VERBOSE=1
export FWUPD_SYSFSFWDIR=${TMPDIR}
export FWUPD_UEFI_ESP_PATH=${TMPDIR}/mnt

# use these to fake a UEFI system
mkdir -p ${FWUPD_SYSFSFWDIR}/efi/efivars
mkdir -p ${FWUPD_UEFI_ESP_PATH}

error() {
    rc=$1
    cat fwupdtool.txt
    exit $rc
}

expect_rc() {
    expected=$1
    rc=$?

    [ "$expected" -eq "$rc" ] || error "$rc"
}

run() {
    cmd="fwupdtool -v $*"
    echo "cmd: $cmd" >fwupdtool.txt
    $cmd 1>>fwupdtool.txt 2>&1
}

# ---
echo "Creating Boot0001"
run efiboot-create 0001 Fedora shimx64.efi ${FWUPD_UEFI_ESP_PATH}
expect_rc 0

# ---
echo "Creating Boot0001 again (should fail)..."
run efiboot-create 0001 Fedora shimx64.efi ${FWUPD_UEFI_ESP_PATH}
expect_rc 2

# ---
echo "Creating Boot0002 with invalid path (should fail)..."
run efiboot-create 0002 Fedora shimx64.efi /mnt/dave
expect_rc 3

# ---
echo "Getting BootOrder (should fail)"
run efiboot-order
expect_rc 3

# ---
echo "Setting BootOrder"
run efiboot-order 0001
expect_rc 0

# ---
echo "Getting BootOrder"
run efiboot-order
expect_rc 0

# ---
echo "Setting BootNext"
run efiboot-next 0001
expect_rc 0

# ---
echo "Getting BootNext"
run efiboot-next
expect_rc 0

# ---
echo "Getting hive cmdline (should fail)"
run efiboot-hive 0001 cmdline
expect_rc 1

# ---
echo "Setting hive cmdline"
run efiboot-hive --force 0001 cmdline acpi=off
expect_rc 0

# ---
echo "Getting hive cmdline"
run efiboot-hive 0001 cmdline
expect_rc 0

# ---
echo "Creating Boot0002 as Win10"
run efiboot-create 0002 Win10 bootmgfw.efi ${FWUPD_UEFI_ESP_PATH}
expect_rc 0

# ---
echo "Setting hive cmdline for Win10 (should fail)"
run efiboot-hive --force 0002 cmdline acpi=off
expect_rc 1

# ---
echo "Showing EFI boot info"
run efiboot-info
expect_rc 0

# ---
echo "Showing EFI boot info (json)"
run efiboot-info --json
expect_rc 0

# ---
echo "Showing EFI files"
run efiboot-files
expect_rc 0

# ---
echo "Deleting Boot0001"
run efiboot-delete 0001
expect_rc 0

# ---
echo "Deleting Boot0001 (should fail)..."
run efiboot-delete 0001
expect_rc 3

# ---
echo "Showing EFI variables"
run efivar-list 8be4df61-93ca-11d2-aa0d-00e098032b8c
expect_rc 0
