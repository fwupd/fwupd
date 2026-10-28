#!/bin/sh
set -eux

# disable the safe directory feature
git config --global safe.directory "*"

# build a minimal library-only configuration and generate the dist tarball
BUILD=build-dist
rm -rf "$BUILD"
meson setup "$BUILD" \
    -Dbuild=library \
    -Ddocs=disabled \
    -Dman=false \
    -Dauto_features=disabled \
    -Dtests=false
meson dist -C "$BUILD"

# copy as artifact
mkdir -p dist
cp "$BUILD"/meson-dist/*.tar.xz dist
echo "version=$(meson introspect "$BUILD" --projectinfo | jq -r .version)" >dist/versioninfo.txt
