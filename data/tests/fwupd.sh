#!/bin/sh -e

exec 0>/dev/null
exec 2>&1

run_device_tests() {
    if [ -n "$CI_NETWORK" ] && [ -d @devicetestdir@ ]; then
        for f in $(grep --files-with-matches -r emulation- @devicetestdir@); do
            echo " ● Emulating for $f"
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

run_umockdev_test() {
    INSPECTOR=@installedtestsdatadir@/unittest_inspector.py
    ARG=@installedtestsdatadir@/$1
    if [ -f ${INSPECTOR} ] && [ -f ${ARG} ]; then
        TESTS=$(${INSPECTOR} ${ARG})
        for test in ${TESTS}; do
            ${ARG} ${test} --verbose
        done
    fi
}

export LSAN_OPTIONS="suppressions=@installedtestsdatadir@/lsan-suppressions.txt"

if [ -d @installedtestsbindir@ ]; then
    for f in @installedtestsbindir@/*-test; do
        $f
    done
fi

run_umockdev_test fwupd_test.py
run_umockdev_test pci_psp_test.py
run_device_tests
fwupdmgr quit

# success!
exit 0
