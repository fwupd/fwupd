#!/bin/sh -e

# if invoked outside of CI
if [ "$CI" != "true" ]; then
    echo "Not running in CI"
    exit 1
fi

gcovr -x \
    --filter build/libfwupd \
    --filter build/libfwupdplugin \
    --filter build/plugins \
    --filter build/src \
    -o coverage.xml
sed "s,build/,,g" coverage.xml -i
