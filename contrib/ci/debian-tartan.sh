#!/usr/bin/env bash
set -euxo pipefail

./contrib/ci/fwupd_setup_helpers.py test-meson

meson setup build
ninja -C build scan-build
