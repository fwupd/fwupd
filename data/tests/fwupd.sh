#!/bin/sh -e

exec 0>/dev/null
exec 2>&1

run_test()
{
        if [ -f @installedtestsbindir@/$1 ]; then
                @installedtestsbindir@/$1
        fi
}

run_device_tests()
{
	if [ -n "$CI_NETWORK" ] && [ -d @devicetestdir@ ]; then
		for f in `grep --files-with-matches -r emulation- @devicetestdir@`; do
		        echo "Emulating for $f"
		        fwupdmgr device-emulate \
				--download-retries=5 \
				--no-unreported-check \
				--no-remote-check \
				--no-metadata-check \
				--json \
				"$f"
		done
	fi
}

run_umockdev_test()
{
	INSPECTOR=@installedtestsdatadir@/unittest_inspector.py
	ARG=@installedtestsdatadir@/$1
	if [ -f ${INSPECTOR} ] && [ -f ${ARG} ]; then
		TESTS=`${INSPECTOR} ${ARG}`
		for test in ${TESTS}; do
			${ARG} ${test} --verbose
		done
	fi
}

export LSAN_OPTIONS="suppressions=@installedtestsdatadir@/lsan-suppressions.txt"

run_test acpi-dmar-self-test
run_test acpi-facp-self-test
run_test acpi-ivrs-self-test
run_test acpi-phat-self-test
run_test ata-self-test
run_test dfu-self-test
run_test fwupdplugin-self-test
run_test linux-swap-self-test
run_test logitech-hidpp-self-test
run_test mtd-self-test
run_test nitrokey-self-test
run_test nvme-self-test
run_test redfish-self-test
run_test synaptics-prometheus-self-test
run_test tpm-self-test
run_test uefi-dbx-self-test
run_test uefi-mok-self-test
run_test vli-self-test
run_test wacom-usb-self-test
run_umockdev_test fwupd_test.py
run_umockdev_test pci_psp_test.py
run_device_tests
fwupdmgr quit

# success!
exit 0
