#!/bin/sh
set -e

# shellcheck disable=SC1091
. "$(dirname "$(readlink -f "$0")")/nix.sh"

VENV="$(dirname "$0")/.."
BUILD="${VENV}/build"
INSTALLED_TESTS="${VENV}/dist/share/installed-tests/fwupd"
SUDO=$(which sudo)
export G_TEST_BUILDDIR="${INSTALLED_TESTS}"
export G_TEST_SRCDIR="${INSTALLED_TESTS}"
export GI_TYPELIB_PATH="${BUILD}/libfwupd"
export LD_LIBRARY_PATH="${BUILD}/libfwupd"
export DAEMON_BUILDDIR="${BUILD}/src"
export PATH="${VENV}/bin:$PATH"
export PYTHONWARNINGS="ignore::DeprecationWarning:gi.events"

echo "Build time test suite"
meson test -C "${BUILD}" "$@"

# If we have arguments to pass to meson test, skip the other tests
if [ $# -eq 0 ]; then
    echo "Testing mtd-self-test"
    "${SUDO}" modprobe mtdram
    "${SUDO}" \
        G_TEST_BUILDDIR="${G_TEST_BUILDDIR}" \
        LD_LIBRARY_PATH="${LD_LIBRARY_PATH}" \
        G_TEST_SRCDIR="${G_TEST_SRCDIR}" \
        "${VENV}"/dist/libexec/installed-tests/fwupd/mtd-self-test

    echo "Testing fwupdtool.sh"
    "${INSTALLED_TESTS}"/fwupdtool.sh

    echo "Starting daemon"
    G_DEBUG=fatal-criticals "${VENV}"/bin/fwupd --verbose --no-timestamp >fwupd.txt 2>&1 &

    echo "Testing fwupd.sh"
    "${INSTALLED_TESTS}"/fwupd.sh

    # artifacts from the test run
    rm -f fwupd.txt fwupdtool.txt
fi
