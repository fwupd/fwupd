#!/bin/sh -ex

pacman -Sy --noconfirm qt5-base gcovr python-flask swtpm tpm2-tools
pacman -U --noconfirm dist/*.pkg.*

# run custom redfish simulator
plugins/redfish/tests/redfish.py &

# run custom snapd simulator
plugins/uefi-dbx/tests/snapd.py &

# run TPM simulator
#swtpm socket --tpm2 --server port=2321 --ctrl type=tcp,port=2322 --flags not-need-init --tpmstate "dir=$PWD" &
#trap 'kill $!' EXIT
# extend a PCR0 value for test suite
#sleep 2
#tpm2_startup -c
#tpm2_pcrextend 0:sha1=f1d2d2f924e986ac86fdf7b36c94bcdf32beec15
# mark as disabled until it is fixed
#export TPM_SERVER_RUNNING=1

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
G_DEBUG=fatal-criticals /usr/lib/fwupd/fwupd --verbose &
sleep 10
/usr/share/installed-tests/fwupd/fwupdmgr.sh
/usr/share/installed-tests/fwupd/fwupd.sh
/usr/share/installed-tests/fwupd/fwupdtool.sh

# generate coverage report
./contrib/ci/coverage.sh
