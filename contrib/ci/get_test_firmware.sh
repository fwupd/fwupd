#!/bin/sh

if [ "$CI_NETWORK" = "true" ]; then
        #clone fwupd-test-firmware
        rm -rf fwupd-test-firmware
        git clone https://github.com/fwupd/fwupd-test-firmware
        #copy data for self-tests into the source tree
        cp fwupd-test-firmware/ci-tests/* . -R
fi
