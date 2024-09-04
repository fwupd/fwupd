#!/bin/sh
set -e
set -x

#check for and install missing dependencies
./contrib/ci/fwupd_setup_helpers.py install-dependencies --yes -o ubuntu

#check we have pip
./contrib/ci/fwupd_setup_helpers.py install-pip --yes -o ubuntu

#check meson is new enough
./contrib/ci/fwupd_setup_helpers.py test-meson

#check markdown is new enough
./contrib/ci/fwupd_setup_helpers.py test-markdown

#check jinja2 is installed
./contrib/ci/fwupd_setup_helpers.py test-jinja2

#clone test firmware if necessary
. ./contrib/ci/get_test_firmware.sh

#evaluate using Ubuntu's buildflags
#evaluate using Debian/Ubuntu's buildflags
#disable link time optimization, Ubuntu currently only sets it for GCC
export DEB_BUILD_MAINT_OPTIONS="optimize=-lto"
eval "$(dpkg-buildflags --export=sh)"
#filter out -Bsymbolic-functions
LDFLAGS=$(dpkg-buildflags --get LDFLAGS | sed "s/-Wl,-Bsymbolic-functions\s//")
export LDFLAGS

root=$(pwd)
export BUILD=${root}/build
rm -rf ${BUILD}
chown -R nobody ${root}
sudo -u nobody meson ${BUILD} -Dman=false -Ddocs=enabled --prefix=${root}/dist
#build with clang
sudo -u nobody ninja -C ${BUILD} test -v

# check for unused symbols
./contrib/ci/check-unused.py

# check the daemon aborts
set +e
FWUPD_SYSCALL_FILTER=systemd ${BUILD}/src/fwupd --immediate-exit
if [ $? -ne 1 ] ; then
    echo "failed to detect missing syscall filtering"
    exit 1
fi

#make docs available outside of docker
ninja -C ${BUILD} install -v
