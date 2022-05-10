#!/bin/sh
set -e

root=$(pwd)
export DESTDIR=${root}/dist
build=$root/build-win32
mkdir -p "$build" && cd "$build"

# install deps
if [ ! -f /usr/share/mingw/toolchain-mingw64.meson ]; then
    ./contrib/ci/fwupd_setup_helpers.py -v mingw64 install-dependencies
fi

# try to keep this and ../contrib/ci/build_windows.sh in sync as much as makes sense
meson .. \
    --cross-file=/usr/share/mingw/toolchain-mingw64.meson \
    --cross-file=../contrib/mingw64.cross \
    --prefix=/ \
    --sysconfdir="etc" \
    --libexecdir="bin" \
    --bindir="bin" \
    -Dbuild=all \
    -Ddocs=none \
    -Dhsi=false \
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
    -Dgusb:vapi=false

# run tests
export WINEPATH="/usr/x86_64-w64-mingw32/sys-root/mingw/bin/;$build/libfwupd/;$build/libfwupdplugin/;$build/subprojects/libxmlb/src/;$build/subprojects/gcab/libgcab/;$build/subprojects/libjcat/libjcat/;$build/subprojects/gusb/gusb/"
ninja -C "$build" install
ninja -C "$build" test

MINGW32BINDIR=/usr/x86_64-w64-mingw32/sys-root/mingw/bin

#disable motd for Windows
sed -i 's,UpdateMotd=.*,UpdateMotd=false,' "$DESTDIR/etc/fwupd/daemon.conf"

# copy deps
cp -f -v \
	$MINGW32BINDIR/gspawn-win64-helper-console.exe \
	$MINGW32BINDIR/gspawn-win64-helper.exe \
	$MINGW32BINDIR/iconv.dll \
	$MINGW32BINDIR/libarchive-13.dll \
	$MINGW32BINDIR/libbrotlicommon.dll \
	$MINGW32BINDIR/libbrotlidec.dll \
	$MINGW32BINDIR/libbz2-1.dll \
	$MINGW32BINDIR/libcrypto-1_1-x64.dll \
	$MINGW32BINDIR/libcurl-4.dll \
	$MINGW32BINDIR/libffi-*.dll \
	$MINGW32BINDIR/libgcc_s_seh-1.dll \
	$MINGW32BINDIR/libgio-2.0-0.dll \
	$MINGW32BINDIR/libglib-2.0-0.dll \
	$MINGW32BINDIR/libgmodule-2.0-0.dll \
	$MINGW32BINDIR/libgmp-10.dll \
	$MINGW32BINDIR/libgnutls-30.dll \
	$MINGW32BINDIR/libgobject-2.0-0.dll \
	$MINGW32BINDIR/libgusb-2.dll \
	$MINGW32BINDIR/libhogweed-*.dll \
	$MINGW32BINDIR/libidn2-0.dll \
	$MINGW32BINDIR/libintl-8.dll \
	$MINGW32BINDIR/libjson-glib-1.0-0.dll \
	$MINGW32BINDIR/liblzma-5.dll \
	$MINGW32BINDIR/libnettle-*.dll \
	$MINGW32BINDIR/libp11-kit-0.dll \
	$MINGW32BINDIR/libpcre-1.dll \
	$MINGW32BINDIR/libsqlite3-0.dll \
	$MINGW32BINDIR/libssh2-1.dll \
	$MINGW32BINDIR/libssl-1_1-x64.dll \
	$MINGW32BINDIR/libssp-0.dll \
	$MINGW32BINDIR/libtasn1-6.dll \
	$MINGW32BINDIR/libusb-1.0.dll \
	$MINGW32BINDIR/libwinpthread-1.dll \
	$MINGW32BINDIR/libxml2-2.dll \
	$MINGW32BINDIR/zlib1.dll \
	"$DESTDIR/bin/"
