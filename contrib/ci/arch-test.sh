#!/bin/sh -e

pacman -U --noconfirm dist/*.pkg.*

#run the CI tests for Qt5
pacman -S --noconfirm qt5-base gcovr
meson qt5-thread-test contrib/ci/qt5-thread-test
ninja -C qt5-thread-test test

#get the test firmware
./contrib/ci/get_test_firmware.sh
cp fwupd-test-firmware/installed-tests/* /usr/share/installed-tests/fwupd/ -LRv

# gnome-desktop-testing is missing, so manually run these tests
export G_TEST_SRCDIR=/usr/share/installed-tests/fwupd G_TEST_BUILDDIR=/usr/share/installed-tests/fwupd
/usr/bin/dbus-daemon --system
/usr/lib/polkit-1/polkitd &
sleep 5

fwupdtool enable-test-devices
/usr/lib/fwupd/fwupd --verbose &
sleep 10
/usr/share/installed-tests/fwupd/fwupdmgr.sh
/usr/share/installed-tests/fwupd/fwupdtool.sh

# generate coverage report
if [ "$CI" = "true" ]; then
	gcovr -x \
		--filter build/libfwupd \
		--filter build/libfwupdplugin \
		--filter build/plugins \
		--filter build/src \
		-o coverage.xml
	sed "s,build/,,g" coverage.xml -i
fi
