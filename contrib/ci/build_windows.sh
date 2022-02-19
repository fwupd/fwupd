#!/bin/sh
set -e
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

#build
mkdir -p $build $DESTDIR && cd $build
python3 -m pip install --user "meson >= 0.60.0"
meson .. \
    --cross-file=../contrib/mingw64.cross \
    --prefix=/ \
    --libexecdir="" \
    --bindir="" \
    -Dbuild=standalone \
    -Ddocs=none \
    -Dhsi=false \
    -Dman=false \
    -Dmetainfo=false \
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
    -Dlibjcat:introspection=false \
    -Dgusb:tests=false \
    -Dgusb:docs=false \
    -Dgusb:introspection=false \
    -Dgusb:vapi=false $@
VERSION=$(meson introspect . --projectinfo | jq -r .version)
ninja -v
ninja -v install

#generate news release
cd $root
contrib/ci/generate_news.py $VERSION > $DESTDIR/news.txt
echo $VERSION > $DESTDIR/VERSION

#disable motd for Windows
sed -i 's,UpdateMotd=.*,UpdateMotd=false,' $DESTDIR/etc/fwupd/daemon.conf

# create a setup binary
cd $DESTDIR
mkdir -p $DESTDIR/setup
makensis -NOCD $build/contrib/setup-win32.nsi

#so that it's actually executable
cp /usr/x86_64-w64-mingw32/sys-root/mingw/bin/*.dll .

#remove static archives
find -type f -print0 -name "*.dll.a" | xargs rm -f

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
