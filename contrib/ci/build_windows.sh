#!/bin/sh
set -e
#prep
export LC_ALL=C.UTF-8
export DESTDIR=`pwd`/dist
build=`pwd`/build-win32
rm -rf $DESTDIR $build

#build
mkdir -p $build $DESTDIR && cd $build
echo $(../contrib/get-version.py) > $DESTDIR/VERSION
meson .. \
    --cross-file=../contrib/mingw64.cross \
    --prefix=/ \
    --libexecdir=$target \
    --bindir=$target \
    -Dbuild=standalone \
    -Dgpg=false \
    -Dplugin_coreboot=false \
    -Dplugin_flashrom=false \
    -Dplugin_uefi=false \
    -Dplugin_redfish=false \
    -Dplugin_altos=false \
    -Dplugin_dell=false \
    -Dplugin_nvme=false \
    -Dplugin_tpm=false \
    -Dsystemd=false \
    -Dplugin_emmc=false \
    -Dplugin_amt=false \
    -Dintrospection=false \
    -Dplugin_thunderbolt=false \
    -Dplugin_synaptics=false \
    -Dman=false \
    -Dgcab:introspection=false \
    -Dgcab:docs=false \
    -Dgcab:nls=false \
    -Dgcab:vapi=false \
    -Dgcab:tests=false \
    -Dlibxmlb:introspection=false \
    -Dlibxmlb:gtkdoc=false \
    -Dgudev=false $@
ninja -v

#prepare archive to run on Windows
ninja -v install
cd $DESTDIR

# create a setup binary
mkdir -p $DESTDIR/setup
makensis -NOCD $build/contrib/setup-win32.nsi

#so that it's actually executable
cp /usr/x86_64-w64-mingw32/sys-root/mingw/bin/*.dll .

#remove static archives
find -type f -name "*.dll.a" | xargs rm -f

#remove stuff that we really don't need
rm -fr gcab.exe \
       xb-tool.exe \
       share/man \
       include \
       fwupd \
       lib/*.a \
       lib/pkgconfig/ \
       var

export WINEPATH="/usr/x86_64-w64-mingw32/sys-root/mingw/bin/;$build/libfwupd/;$build/subprojects/libxmlb/src/;$build/subprojects/gcab/libgcab/"
#TODO: fixup tests
ninja -C $build test || true
