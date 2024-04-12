#!/bin/bash -e

# install
apt update
apt install -y gcovr ./dist/*.deb

# run tests
./contrib/ci/get_test_firmware.sh
cp fwupd-test-firmware/installed-tests/* /usr/share/installed-tests/fwupd/ -LRv
service dbus restart
gnome-desktop-testing-runner fwupd

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

# cleanup
apt purge -y fwupd fwupd-doc libfwupd3 libfwupd-dev
