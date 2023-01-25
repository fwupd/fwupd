#!/bin/sh

exec 2>&1

run_test()
{
        if [ -f @installedtestsbindir@/$1 ]; then
                @installedtestsbindir@/$1
                rc=$?; if [ $rc != 0 ]; then exit $rc; fi
        fi
}

run_test acpi-dmar-self-test
run_test acpi-facp-self-test
run_test acpi-phat-self-test
run_test ata-self-test
run_test nitrokey-self-test
run_test linux-swap-self-test
run_test nvme-self-test
run_test wacom-usb-self-test
run_test redfish-self-test
run_test optionrom-self-test
run_test vli-self-test
run_test uefi-dbx-self-test
run_test synaptics-prometheus-self-test
run_test dfu-self-test
run_test mtd-self-test

# grab device tests from the CDN to avoid incrementing the download counter
export FWUPD_DEVICE_TESTS_BASE_URI=http://cdn.fwupd.org/downloads
for f in `grep --files-with-matches -r emulation-url @devicetestdir@`; do
        echo "Emulating for $f"
        fwupdmgr --force device-test --no-unreported-check --no-remote-check --no-metadata-check "$f"
        rc=$?; if [ $rc != 0 ]; then exit $rc; fi
done

# success!
exit 0
