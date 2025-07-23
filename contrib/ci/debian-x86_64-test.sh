#!/bin/bash -e

# install
apt update
apt install -y gcovr ./dist/*.deb

fwupdtool enable-test-devices

# run tests
./contrib/ci/get_test_firmware.sh /usr/share/installed-tests/fwupd/
service dbus restart
gnome-desktop-testing-runner --timeout=600 fwupd

# generate coverage report
./contrib/ci/coverage.sh

# cleanup
apt purge -y fwupd fwupd-doc libfwupd3 libfwupd-dev
