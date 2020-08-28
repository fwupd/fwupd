#!/bin/sh

#clone fwupd-test-firmware
rm -rf fwupd-test-firmware
git clone https://github.com/fwupd/fwupd-test-firmware
#set up build tests to work
cp fwupd-test-firmware/ci-tests/* . -R
export G_TEST_SRCDIR=`pwd`/fwupd-test-firmware/installed-tests
