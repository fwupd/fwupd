#!/bin/bash -e

apt install -y ./*.deb
./contrib/ci/get_test_firmware.sh
cp fwupd-test-firmware/installed-tests/* /usr/share/installed-tests/fwupd/ -LRv
service dbus restart
gnome-desktop-testing-runner fwupd
apt purge -y fwupd fwupd-doc libfwupd3 libfwupd-dev
