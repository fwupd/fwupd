#!/bin/sh -e

pacman -U --noconfirm dist/*.pkg.*

#run the CI tests for Qt5
pacman -S --noconfirm qt5-base
meson qt5-thread-test contrib/ci/qt5-thread-test
ninja -C qt5-thread-test test

#get the test firmware
./contrib/ci/get_test_firmware.sh
cp fwupd-test-firmware/installed-tests/* /usr/share/installed-tests/fwupd/ -LRv

# gnome-desktop-testing is missing, so manually run these tests
export G_TEST_SRCDIR=/usr/share/installed-tests/fwupd G_TEST_BUILDDIR=/usr/share/installed-tests/fwupd
/usr/bin/dbus-daemon --system
fwupdtool enable-test-devices
/usr/lib/fwupd/fwupd --verbose &
sleep 10
/usr/share/installed-tests/fwupd/fwupdmgr.sh
/usr/share/installed-tests/fwupd/fwupdtool.sh
/usr/share/installed-tests/fwupd/fwupd.sh
