#!/bin/sh

exec 2>&1
dirname=`dirname $0`

run_test()
{
        if [ -f $dirname/$1 ]; then
                $dirname/$1
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

# success!
exit 0
