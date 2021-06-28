#!/bin/sh

GITHUB_SHA=
GITHUB_REPOSITORY=
GITHUB_REPOSITORY_OWNER=
GITHUB_TAG=

while [ -n "$1" ]; do
    case $1 in
        --GITHUB_SHA=*)
            GITHUB_SHA=${1#--GITHUB_SHA=}
            ;;
        --GITHUB_REPOSITORY=*)
            GITHUB_REPOSITORY=${1#--GITHUB_REPOSITORY=}
            ;;
        --GITHUB_REPOSITORY_OWNER=*)
            GITHUB_REPOSITORY_OWNER=${1#--GITHUB_REPOSITORY_OWNER=}
            ;;
        --GITHUB_TAG=*)
            GITHUB_TAG=${1#--GITHUB_TAG=}
            ;;
        *)
            echo "Command $1 unknown. exiting..."
            exit 1
            ;;
    esac
    shift
done

if [ -z "$GITHUB_SHA" ] || [ -z "$GITHUB_REPOSITORY" ] || \
    [ -z "$GITHUB_REPOSITORY_OWNER" ] || [ -z "$GITHUB_TAG" ]; then
    exit 1
fi

# Include-file of libefivar port uses GCC-specific builtin function
export CC=gcc

set -xe
mkdir -p /usr/local/etc/pkg/repos/
# Fix meson flag problem https://www.mail-archive.com/freebsd-ports@freebsd.org/msg86617.html
cp /etc/pkg/FreeBSD.conf /usr/local/etc/pkg/repos/FreeBSD.conf
sed -i .old 's|url: "pkg+http://pkg.FreeBSD.org/${ABI}/quarterly"|url: "pkg+http://pkg.FreeBSD.org/${ABI}/latest"|' \
/usr/local/etc/pkg/repos/FreeBSD.conf
pkg install -y meson efivar
pkg upgrade -y meson
cd /usr
git clone https://github.com/3mdeb/freebsd-ports.git --depth 1 -b fwupd ports
cd /usr/ports/sysutils/fwupd
rm -rf ./*
ls .
cp -r ~/work/fwupd/fwupd/contrib/freebsd/* .
ls .
REPOSITORY_NAME=$(echo "${GITHUB_REPOSITORY}" | awk -F / '{print $2}')
sed -i .old "s/GH_TAGNAME=.*$/GH_TAGNAME=\t${GITHUB_SHA}/" Makefile
sed -i .old "s/GH_ACCOUNT=.*$/GH_ACCOUNT=\t${GITHUB_REPOSITORY_OWNER}/" Makefile
sed -i .old "s/DISTVERSION=.*$/DISTVERSION=\t${GITHUB_TAG}/" Makefile
make makesum
make clean
make
make makeplist > plist
sed -i "" "1d" plist
make package
make install
cp /usr/ports/sysutils/fwupd/work/pkg/fwupd*.txz \
~/work/fwupd/fwupd/fwupd-freebsd-${GITHUB_TAG}-${GITHUB_SHA}.txz || exit 1
