#!/usr/bin/env bash
set -euxo pipefail

meson setup build
ninja -C build scan-build
