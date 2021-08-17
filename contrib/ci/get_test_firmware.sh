#!/bin/sh

if [ "$CI_NETWORK" = "true" ]; then
        #clone fwupd-test-firmware
        rm -rf fwupd-test-firmware
        git clone https://github.com/fwupd/fwupd-test-firmware
        #set up build tests to work
        cp fwupd-test-firmware/ci-tests/* . -R
        root=$(pwd)
        export G_TEST_SRCDIR=${root}/fwupd-test-firmware/installed-tests
fi
