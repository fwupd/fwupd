#!/bin/sh
set -e

# if invoked outside of CI
if [ "$CI" != "true" ]; then
    echo "Not running in CI, please manually configure Windows build"
    exit 1
fi

# install deps
if [ "$(id -u)" -eq 0 ]; then
    dnf install -y python3
    dnf install -y xvfb-run
    ./contrib/ci/fwupd_setup_helpers.py --yes -o fedora -v mingw64 install-dependencies
    dnf install -y https://kojipkgs.fedoraproject.org//packages/msitools/0.106/1.fc42/x86_64/msitools-0.106-1.fc42.x86_64.rpm
fi

#prep
export LC_ALL=C.UTF-8
root=$(pwd)
export DESTDIR=${root}/dist
build=$root/build-win32

rm -rf $DESTDIR $build
mkdir -p $build $DESTDIR && cd $build

# Hack for Fedora bug
if [ "$(id -u)" -eq 0 ]; then
    sed -i '/^Requires.private: termcap/d'  /usr/x86_64-w64-mingw32/sys-root/mingw/lib/pkgconfig/readline.pc
fi

# run before using meson
export WINEPREFIX=$build/.wine

# For logitech bulk controller being disabled (-Dprotobuf=disabled):
# See https://bugzilla.redhat.com/show_bug.cgi?id=1991749
# When fixed need to do the following to enable:
# 1. need to add mingw64-protobuf mingw64-protobuf-tools to CI build deps
# 2. add protoc = /path/to/protoc-c.exe in mingw64.cross
# 3. Only enable when not a tagged release (Unsupported by Logitech)

# try to keep this and ../contrib/build-windows.sh in sync as much as makes sense
xvfb-run meson setup .. \
    --cross-file=/usr/share/mingw/toolchain-mingw64.meson \
    --cross-file=../contrib/mingw64.cross \
    --prefix=/ \
    --sysconfdir="etc" \
    --libexecdir="bin" \
    --bindir="bin" \
    -Dbuild=all \
    -Dman=false \
    -Dtests=false \
    -Dbuildtype=release \
    -Ddbus_socket_address="tcp:host=localhost,port=1341" \
    -Dfish_completion=false \
    -Dbash_completion=false \
    -Dfirmware-packager=false \
    -Dmetainfo=false \
    -Dpassim=disabled \
    -Dlibjcat:man=false \
    -Dlibjcat:gpg=false \
    -Dlibjcat:tests=false \
    -Dlibjcat:introspection=false \
    $@
VERSION=$(meson introspect . --projectinfo | jq -r .version)
ninja --verbose -C "$build" -v install

#disable motd for Windows
cd $root
sed -i 's,UpdateMotd=.*,UpdateMotd=false,' "$DESTDIR/etc/fwupd/fwupd.conf"

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
	-o -name libcrypto-3-x64.dll \
	-o -name libcurl-4.dll \
	-o -name "libffi-*.dll" \
	-o -name libgcc_s_seh-1.dll \
	-o -name libgio-2.0-0.dll \
	-o -name libglib-2.0-0.dll \
	-o -name libgmodule-2.0-0.dll \
	-o -name libgmp-10.dll \
	-o -name libgnutls-30.dll \
	-o -name libgobject-2.0-0.dll \
	-o -name "libhogweed-*.dll" \
	-o -name libidn2-0.dll \
	-o -name libintl-8.dll \
	-o -name libjson-glib-1.0-0.dll \
	-o -name liblzma-5.dll \
	-o -name "libnettle-*.dll" \
	-o -name libp11-kit-0.dll \
	-o -name libpcre2-8-0.dll \
	-o -name libpsl-5.dll \
	-o -name libsqlite3-0.dll \
	-o -name libssh2-1.dll \
	-o -name libssl-3-x64.dll \
	-o -name libssp-0.dll \
	-o -name libtermcap-0.dll \
	-o -name libreadline8.dll \
	-o -name libtasn1-6.dll \
	-o -name libunistring-2.dll \
	-o -name libusb-1.0.dll \
	-o -name libwinpthread-1.dll \
	-o -name libxml2-2.dll \
	-o -name libxmlb-2.dll \
	-o -name libzstd.dll \
	-o -name wldap32.dll \
	-o -name zlib1.dll \
	| wixl-heat \
	-p $MINGW32BINDIR/ \
	--win64 \
	--directory-ref BINDIR \
	--var "var.MINGW32BINDIR" \
	--component-group "CG.fwupd-deps" | \
	tee $build/contrib/fwupd-deps.wxs

echo $CERTDIR/ca-bundle.crt \
	| wixl-heat \
	-p $CERTDIR/ \
	--win64 \
	--directory-ref BINDIR \
	--var "var.CERTDIR" \
	--component-group "CG.fwupd-crts" | \
	tee $build/contrib/fwupd-crts.wxs

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

#add service install key
sed -i "$build/contrib/fwupd-files.wxs" -f - << EOF
s,fwupd.exe"/>,fwupd.exe"/>\\
          <ServiceInstall Id="FwupdServiceInstaller" Interactive="no" ErrorControl="normal" Name="fwupd" DisplayName="fwupd" Description="fwupd" Start="auto" Type="ownProcess" Arguments=""/>,
EOF

MSI_FILENAME="$DESTDIR/setup/fwupd-$VERSION-setup-x86_64.msi"
mkdir -p "$DESTDIR/setup"
wixl -v \
	"$build/contrib/fwupd.wxs" \
	"$build/contrib/fwupd-crts.wxs" \
	"$build/contrib/fwupd-deps.wxs" \
	"$build/contrib/fwupd-files.wxs" \
	-D CERTDIR=$CERTDIR \
	-D MINGW32BINDIR=$MINGW32BINDIR \
	-D Win64="yes" \
	-D DESTDIR="$DESTDIR" \
	-o "${MSI_FILENAME}"

# check the msi archive can be installed and removed (use "wine uninstaller" to do manually)
wine msiexec /i "${MSI_FILENAME}"
ls -R ${WINEPREFIX}/drive_c/Program\ Files/fwupd/
wine ${WINEPREFIX}/drive_c/Program\ Files/fwupd/bin/fwupdtool.exe get-plugins --json
wine msiexec /x "${MSI_FILENAME}"
