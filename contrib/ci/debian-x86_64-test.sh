#!/bin/bash -e

# install
apt update
apt install -y gcovr ./dist/*.deb

fwupdtool enable-test-devices
fwupdtool emulation-tag 08d460be0f1f9f128413f816022a6439e0078018

# run tests
./contrib/ci/get_test_firmware.sh
cp fwupd-test-firmware/installed-tests/* /usr/share/installed-tests/fwupd/ -LRv
service dbus restart
gnome-desktop-testing-runner fwupd

# generate coverage report
./contrib/ci/coverage.sh

# cleanup
apt purge -y fwupd fwupd-doc libfwupd3 libfwupd-dev
