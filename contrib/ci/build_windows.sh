#!/bin/sh
set -e

# if invoked outside of CI
if [ "$CI" != "true" ]; then
    echo "Not running in CI, please manually configure Windows build"
    exit 1
fi

# install deps
./contrib/ci/fwupd_setup_helpers.py --yes -o fedora -v mingw64 install-dependencies

# update to latest version of meson
if [ "$(id -u)" -eq 0 ]; then
    dnf install -y python-pip
    pip install meson --force-reinstall
fi

#prep
export LC_ALL=C.UTF-8
root=$(pwd)
export DESTDIR=${root}/dist
build=$root/build-win32

rm -rf $DESTDIR $build

# For logitech bulk controller being disabled (-Dplugin_logitech_bulkcontroller=disabled):
# See https://bugzilla.redhat.com/show_bug.cgi?id=1991749
# When fixed need to do the following to enable:
# 1. need to add mingw64-protobuf mingw64-protobuf-tools to CI build deps
# 2. add protoc = /path/to/protoc-c.exe in mingw64.cross
# 3. Only enable when not a tagged release (Unsupported by Logitech)

# try to keep this and ../contrib/build-windows.sh in sync as much as makes sense
mkdir -p $build $DESTDIR && cd $build
meson .. \
    --cross-file=/usr/share/mingw/toolchain-mingw64.meson \
    --cross-file=../contrib/mingw64.cross \
    --prefix=/ \
    --sysconfdir="etc" \
    --libexecdir="bin" \
    --bindir="bin" \
    -Dbuild=all \
    -Dman=false \
    -Dfish_completion=false \
    -Dbash_completion=false \
    -Dfirmware-packager=false \
    -Dmetainfo=false \
    -Dcompat_cli=false \
    -Dsoup_session_compat=false \
    -Dgcab:introspection=false \
    -Dgcab:docs=false \
    -Dgcab:nls=false \
    -Dgcab:vapi=false \
    -Dgcab:tests=false \
    -Dlibxmlb:introspection=false \
    -Dlibxmlb:gtkdoc=false \
    -Dlibjcat:man=false \
    -Dlibjcat:gpg=false \
    -Dlibjcat:tests=false \
    -Dlibjcat:introspection=false \
    -Dgusb:tests=false \
    -Dgusb:docs=false \
    -Dgusb:introspection=false \
    -Dgusb:vapi=false $@
VERSION=$(meson introspect . --projectinfo | jq -r .version)

# run tests
export WINEPATH="/usr/x86_64-w64-mingw32/sys-root/mingw/bin/;$build/libfwupd/;$build/libfwupdplugin/;$build/subprojects/libxmlb/src/;$build/subprojects/gcab/libgcab/;$build/subprojects/libjcat/libjcat/;$build/subprojects/gusb/gusb/"
ninja --verbose -C "$build" -v
ninja -C "$build" test

# switch to release optimizations
meson configure -Dtests=false -Dbuildtype=release
ninja -C "$build" -v install

#disable motd for Windows
cd $root
sed -i 's,UpdateMotd=.*,UpdateMotd=false,' "$DESTDIR/etc/fwupd/daemon.conf"

# create a setup binary
CERTDIR=/etc/pki/tls/certs
MINGW32BINDIR=/usr/x86_64-w64-mingw32/sys-root/mingw/bin

# deps
find $MINGW32BINDIR \
	-name gspawn-win64-helper-console.exe \
	-o -name gspawn-win64-helper.exe \
	-o -name iconv.dll \
	-o -name libarchive-13.dll \
	-o -name libbrotlicommon.dll \
	-o -name libbrotlidec.dll \
	-o -name libbz2-1.dll \
	-o -name libcrypto-1_1-x64.dll \
	-o -name libcurl-4.dll \
	-o -name "libffi-*.dll" \
	-o -name libgcc_s_seh-1.dll \
	-o -name libgio-2.0-0.dll \
	-o -name libglib-2.0-0.dll \
	-o -name libgmodule-2.0-0.dll \
	-o -name libgmp-10.dll \
	-o -name libgnutls-30.dll \
	-o -name libgobject-2.0-0.dll \
	-o -name libgusb-2.dll \
	-o -name "libhogweed-*.dll" \
	-o -name libidn2-0.dll \
	-o -name libintl-8.dll \
	-o -name libjson-glib-1.0-0.dll \
	-o -name liblzma-5.dll \
	-o -name "libnettle-*.dll" \
	-o -name libp11-kit-0.dll \
	-o -name libpcre-1.dll \
	-o -name libsqlite3-0.dll \
	-o -name libssh2-1.dll \
	-o -name libssl-1_1-x64.dll \
	-o -name libssp-0.dll \
	-o -name libtasn1-6.dll \
	-o -name libusb-1.0.dll \
	-o -name libwinpthread-1.dll \
	-o -name libxml2-2.dll \
	-o -name zlib1.dll \
	| wixl-heat \
	-p $MINGW32BINDIR/ \
	--win64 \
	--directory-ref BINDIR \
	--var "var.MINGW32BINDIR" \
	--component-group "CG.fwupd-deps" | \
	tee $build/contrib/fwupd-deps.wxs

# no static libraries
find "$DESTDIR/" -type f -name "*.a" -print0 | xargs rm -f

# our files
find "$DESTDIR" | \
	wixl-heat \
	-p "$DESTDIR/" \
	-x include/ \
	-x share/fwupd/device-tests/ \
	-x share/tests/ \
	-x share/man/ \
	-x share/doc/ \
	-x lib/pkgconfig/ \
	--win64 \
	--directory-ref INSTALLDIR \
	--var "var.DESTDIR" \
	--component-group "CG.fwupd-files" | \
	tee "$build/contrib/fwupd-files.wxs"

MSI_FILENAME="$DESTDIR/setup/fwupd-$VERSION-setup-x86_64.msi"
mkdir -p "$DESTDIR/setup"
wixl -v \
	"$build/contrib/fwupd.wxs" \
	"$build/contrib/fwupd-deps.wxs" \
	"$build/contrib/fwupd-files.wxs" \
	-D CRTDIR=$CERTDIR \
	-D MINGW32BINDIR=$MINGW32BINDIR \
	-D Win64="yes" \
	-D DESTDIR="$DESTDIR" \
	-o "${MSI_FILENAME}"

# check the msi archive can be installed and removed (use "wine uninstaller" to do manually)
# wine msiexec /i "${MSI_FILENAME}"
# ls -R ~/.wine/drive_c/Program\ Files/fwupd/
# wine ~/.wine/drive_c/Program\ Files/fwupd/bin/fwupdtool get-plugins --json
# wine msiexec /x "${MSI_FILENAME}"

#generate news release
contrib/ci/generate_news.py $VERSION > $DESTDIR/news.txt
echo $VERSION > $DESTDIR/VERSION
