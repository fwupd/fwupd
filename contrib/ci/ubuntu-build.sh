#!/bin/sh
set -e
set -x

#clone test firmware if necessary
if [ "${CI_NETWORK:-false}" = "true" ]; then
    ./fwupbuild download-test-firmware --tests ci-tests --dest "$PWD"
fi

#evaluate using Ubuntu's buildflags
#evaluate using Debian/Ubuntu's buildflags
#disable link time optimization, Ubuntu currently only sets it for GCC
#disable -ffile-prefix-map as gcov then records relative source paths for gcovr
export DEB_BUILD_MAINT_OPTIONS="optimize=-lto reproducible=-fixfilepath"
eval "$(dpkg-buildflags --export=sh)"
#filter out -Bsymbolic-functions
LDFLAGS=$(dpkg-buildflags --get LDFLAGS | sed "s/-Wl,-Bsymbolic-functions\s//")
export LDFLAGS

if [ -n "$CARGO_HOME" ]; then
    SUDO_ENV="CARGO_HOME=$CARGO_HOME"
    mkdir -p "$CARGO_HOME"
    chown -R nobody "$CARGO_HOME"
fi

root=$(pwd)
export BUILD="${root}/build"
rm -rf "${BUILD}"
chown -R nobody "${root}"
sudo -u nobody ${SUDO_ENV:-} \
    meson setup "${BUILD}" \
    -Db_coverage=true \
    -Dman=false \
    -Ddocs=enabled \
    -Dlibxmlb:gtkdoc=false \
    --prefix="${root}/target"
#build with clang
sudo -u nobody ${SUDO_ENV:-} ninja -C "${BUILD}" -v
sudo -u nobody ${SUDO_ENV:-} meson test -C "${BUILD}" --print-errorlogs --verbose

# check meson install tags look fine
./contrib/ci/check-meson-install-tags.py -C "$BUILD" check

# check we've not become a CPU or memory hog
ninja -C "${BUILD}" install -v
./contrib/ci/check-rss.py --limit 3072 "${BUILD}/src/fwupdtool" get-devices
./contrib/ci/check-cpu.py --limit 350 "${BUILD}/src/fwupdtool" get-devices

# check the daemon aborts
set +e
FWUPD_SYSCALL_FILTER=systemd "${BUILD}/src/fwupd" --immediate-exit
if [ $? -ne 1 ]; then
    echo "failed to detect missing syscall filtering"
    exit 1
fi

#make docs available outside of docker
mkdir -p "${root}/dist/share"
mv "${root}/target/share/doc" "${root}/dist/share"

# generate coverage report
./contrib/ci/coverage.py
