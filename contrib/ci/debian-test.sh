#!/usr/bin/env bash
set -euo pipefail

# Set up fatal-criticals systemd override
SYSTEMD_OVERRIDE="/etc/systemd/system/fwupd.service.d"
mkdir -p "$SYSTEMD_OVERRIDE"
cat >"$SYSTEMD_OVERRIDE/override.conf" <<EOF
[Service]
Environment="G_DEBUG=fatal-criticals"
EOF

# install
apt update
apt install -y gcovr ./dist/*.deb

fwupdtool enable-test-devices
fwupdtool emulation-tag 08d460be0f1f9f128413f816022a6439e0078018

# run tests
./contrib/ci/get_test_firmware.sh /usr/share/installed-tests/fwupd/
service dbus restart
gnome-desktop-testing-runner --timeout=1200 fwupd

# generate coverage report
./contrib/ci/coverage.sh

# cleanup
apt purge -y fwupd fwupd-doc libfwupd3 libfwupd-dev
