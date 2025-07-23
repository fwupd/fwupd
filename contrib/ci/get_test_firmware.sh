#!/bin/sh

if [ "$CI_NETWORK" = "true" ]; then
        #clone fwupd-test-firmware
        rm -rf fwupd-test-firmware
        git clone https://github.com/fwupd/fwupd-test-firmware
        #If argument is set copy for installed tests
        if [ -n "$1" ]; then
                cp fwupd-test-firmware/installed-tests/* $1 -LRv
        #copy data for self-tests into the source tree
        else
                cp fwupd-test-firmware/ci-tests/* . -Rv
        fi
        rm -rf fwupd-test-firmware
fi
