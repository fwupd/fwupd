#!/bin/sh -ex

pacman -Sy --noconfirm qt5-base gcovr python-flask swtpm tpm2-tools
pacman -U --noconfirm dist/*.pkg.*

# run custom redfish simulator
plugins/redfish/tests/redfish.py &

# run custom snapd simulator
plugins/uefi-dbx/tests/snapd.py --datadir /usr/share/installed-tests/fwupd/tests &

# run TPM simulator
export TPM2TOOLS_TCTI=swtpm:host=127.0.0.1,port=2321
swtpm socket --tpm2 --server port=2321 --ctrl type=tcp,port=2322 --flags not-need-init,startup-clear --tpmstate "dir=$PWD" &
SWTPM_PID=$!
trap 'kill $SWTPM_PID' EXIT
# extend a PCR0 value for test suite
sleep 2
tpm2_pcrextend 0:sha1=f1d2d2f924e986ac86fdf7b36c94bcdf32beec15 0:sha256=722961af9796ab090ace25e9c341aa3177bf2fd7b411c65c661599a84e5feef8 0:sha384=6b38548127fa865ff6fa81cbee64f3b18c7fa01490c700a66e4c8acc4a197db30d83dbb4d4dbb61099baf14490c7681d 0:sha512=d636e21e2eb4b1effd3cc6e3bb40ddf01bb1ecef7e43c8b0e480ba887a12780f70f0e0b7342f1d3c7e4e87849ecd5810930822560ad8e02d7cad8a206df12e1d

#run the CI tests for Qt5
meson qt5-thread-test contrib/ci/qt5-thread-test --werror -Db_coverage=true
ninja -C qt5-thread-test test

#get the test firmware
./contrib/ci/get_test_firmware.sh /usr/share/installed-tests/fwupd/

# gnome-desktop-testing is missing, so manually run these tests
export G_TEST_SRCDIR=/usr/share/installed-tests/fwupd G_TEST_BUILDDIR=/usr/share/installed-tests/fwupd
mkdir -p /run/dbus
/usr/bin/dbus-daemon --system
/usr/lib/polkit-1/polkitd &
sleep 5

fwupdtool enable-test-devices
# tag test device for emulation before starting daemon
fwupdtool emulation-tag 08d460be0f1f9f128413f816022a6439e0078018
NO_COLOR=1 G_DEBUG=fatal-criticals /usr/lib/fwupd/fwupd --verbose --no-timestamp >fwupd.txt 2>&1 &
sleep 10
/usr/share/installed-tests/fwupd/fwupdmgr.sh
/usr/share/installed-tests/fwupd/fwupd.sh
/usr/share/installed-tests/fwupd/fwupdtool.sh
/usr/share/installed-tests/fwupd/fwupdtool-efiboot.sh

# generate coverage report
./contrib/ci/coverage.sh
