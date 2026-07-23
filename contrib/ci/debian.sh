#!/usr/bin/env bash
set -euxo pipefail

# Set Qubes Os vars if -Dqubes=true is parameter
if [ "${QUBES:-false}" = "true" ]; then
    QUBES_OPTION='-Dqubes=true'
else
    QUBES_OPTION=
fi
export QUBES_OPTION

if [[ "${VARIANT:-}" == cross-* ]]; then
    export CROSS=${VARIANT#cross-}
fi

./contrib/ci/fwupd_setup_helpers.py test-meson

#prepare
export DEBFULLNAME="CI Builder"
export DEBEMAIL="ci@travis-ci.org"
VERSION=$(head meson.build | grep ' version:' | cut -d \' -f2)
rm -rf build/
mkdir -p build
shopt -s extglob
cp -R !(build|dist|venv) build/
pushd build
mv contrib/debian .
sed -i 's/quilt/native/' debian/source/format
#generate control file
./contrib/ci/generate_debian.py

# generate a Rust cross file for cross builds
if [ -n "${CROSS:-}" ]; then
    # Map Debian GNU type (e.g. s390x-linux-gnu) to Rust target (s390x-unknown-linux-gnu)
    GNU_TYPE=$(dpkg-architecture -a"${CROSS}" -qDEB_HOST_GNU_TYPE)
    RUST_TARGET=$(echo "$GNU_TYPE" | sed 's/-linux-/-unknown-linux-/')
    printf '[binaries]\nrust = ['\''rustc'\'', '\''--target'\'', '\''%s'\'']\n' "$RUST_TARGET" \
        >debian/rust-cross.ini
fi

# check if we have all deps available
apt update -qq && apt install python3-apt -y
./contrib/ci/fwupd_setup_helpers.py install-dependencies -o debian --yes \
    ${CROSS:+--variant "$CROSS" --cross} ||
    true

dpkg --print-architecture
dpkg --print-foreign-architectures
dpkg-checkbuilddeps ${CROSS:+-a $CROSS}

#disable unit tests if fwupd is already installed (may cause problems)
if [ -x /usr/lib/fwupd/fwupd ]; then
    export DEB_BUILD_OPTIONS=nocheck
fi

if [ ! -z "${CROSS:-}" ]; then
    export DEB_BUILD_OPTIONS=nocheck
fi

#build the package
EDITOR=/bin/true dch --create --package fwupd -v "$VERSION" "CI Build"
debuild --no-lintian -e CI -e CC -e QUBES_OPTION ${CROSS:+-a$CROSS}

#check lintian output
#suppress tags that are side effects of building in docker this way
lintian ../*changes \
    -IE \
    --pedantic \
    --tag-display-limit 0 \
    --suppress-tags missing-build-dependency-for-dh-addon \
    --suppress-tags bad-distribution-in-changes-file \
    --suppress-tags source-nmu-has-incorrect-version-number \
    --suppress-tags no-symbols-control-file \
    --suppress-tags gzip-file-is-not-multi-arch-same-safe \
    --suppress-tags missing-dependency-on-libc \
    --suppress-tags arch-dependent-file-not-in-arch-specific-directory \
    --suppress-tags package-installs-ieee-data \
    --allow-root

#place built packages in dist outside docker
mkdir -p ../dist
find .. -type f -name "*.deb" -print0 | xargs -0 cp -t ../dist
