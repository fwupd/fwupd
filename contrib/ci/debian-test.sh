#!/usr/bin/env bash
set -euox pipefail

group() {
    { echo "${CI:+::group::}🔵 $*"; } 2>/dev/null
}

endgroup() {
    { [ -n "${CI:-}" ] && echo "::endgroup::" || true; } 2>/dev/null
}

# Set up fatal-criticals systemd override
SYSTEMD_OVERRIDE="/etc/systemd/system/fwupd.service.d"
mkdir -p "$SYSTEMD_OVERRIDE"
cat >"$SYSTEMD_OVERRIDE/override.conf" <<EOF
[Service]
Environment="G_DEBUG=fatal-criticals"
EOF

group "Update system and install dependencies"
apt update -qq
apt install -qq -y gcovr ./dist/*.deb
endgroup

group "Enable test devices"
fwupdtool enable-test-devices
endgroup

fwupdtool emulation-tag 08d460be0f1f9f128413f816022a6439e0078018

group "Get the test firmware"
./contrib/ci/get_test_firmware.sh /usr/share/installed-tests/fwupd/
endgroup

service dbus restart

group "Run the tests via gnome-desktop-testing-runner"
gnome-desktop-testing-runner --timeout=2400 fwupd
endgroup

# generate coverage report
./contrib/ci/coverage.sh

group "Clean up"
apt purge -y fwupd fwupd-doc libfwupd3 libfwupd-dev
endgroup
