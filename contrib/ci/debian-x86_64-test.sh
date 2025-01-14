#!/bin/bash -e

# Set up fatal-criticals systemd override
SYSTEMD_OVERRIDE="/etc/systemd/system/fwupd.service.d"
mkdir -p ${SYSTEMD_OVERRIDE}
cp contrib/fwupd-systemd-fatal-criticals.conf ${SYSTEMD_OVERRIDE}/override.conf

# install
apt update
apt install -y gcovr ./dist/*.deb

fwupdtool enable-test-devices
fwupdtool emulation-tag 08d460be0f1f9f128413f816022a6439e0078018

# run tests
./contrib/ci/get_test_firmware.sh /usr/share/installed-tests/fwupd/
service dbus restart
gnome-desktop-testing-runner fwupd

# generate coverage report
./contrib/ci/coverage.sh

# cleanup
apt purge -y fwupd fwupd-doc libfwupd3 libfwupd-dev
