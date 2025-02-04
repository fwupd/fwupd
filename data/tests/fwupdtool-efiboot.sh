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

error()
{
       rc=$1
       cat fwupdtool.txt
       exit $rc
}

run()
{
       cmd="fwupdtool -v $*"
       echo "cmd: $cmd" >fwupdtool.txt
       $cmd 1>>fwupdtool.txt 2>&1
}

# ---
echo "Creating Boot0001"
run efiboot-create 0001 Fedora shimx64.efi ${FWUPD_UEFI_ESP_PATH}
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Creating Boot0001 again (should fail)..."
run efiboot-create 0001 Fedora shimx64.efi ${FWUPD_UEFI_ESP_PATH}
rc=$?; if [ $rc != 2 ]; then error $rc; fi

# ---
echo "Setting BootOrder"
run efiboot-order 0001
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Setting BootNext"
run efiboot-next 0001
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Showing EFI boot info"
run efiboot-info
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Showing EFI files"
run efiboot-files
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Deleting Boot0001"
run efiboot-delete 0001
rc=$?; if [ $rc != 0 ]; then error $rc; fi

# ---
echo "Deleting Boot0001 (should fail)..."
run efiboot-delete 0001
rc=$?; if [ $rc != 3 ]; then error $rc; fi

# ---
echo "Showing EFI variables"
run efivar-list 8be4df61-93ca-11d2-aa0d-00e098032b8c
rc=$?; if [ $rc != 0 ]; then error $rc; fi
