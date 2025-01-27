#!/bin/sh -e

VENV=$(dirname $0)/..
BUILD=${VENV}/build
INSTALLED_TESTS=${VENV}/dist/share/installed-tests/fwupd
export G_TEST_BUILDDIR=${INSTALLED_TESTS}
export G_TEST_SRCDIR=${INSTALLED_TESTS}
export LIBFWUPD_BUILD_DIR=${BUILD}/libfwupd
export DAEMON_BUILDDIR=${BUILD}/src
export PATH=${VENV}/bin:$PATH

echo "Build time test suite"
ninja -C ${BUILD} test

echo "Testing fwupdtool.sh"
${INSTALLED_TESTS}/fwupdtool.sh

echo "Starting daemon"
G_DEBUG=fatal-criticals ${VENV}/bin/fwupd --verbose --no-timestamp >fwupd.txt 2>&1 &

echo "Testing fwupd.sh"
${INSTALLED_TESTS}/fwupd.sh

# artifacts from the test run
rm -f fwupd.txt fwupdtool.txt
