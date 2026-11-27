#!/bin/sh
set -e
set -x

git config --global --add safe.directory "$(pwd)"

# only rebuild the fwupd part, the other parts are restored from the cache
snapcraft clean --destructive-mode fwupd
snapcraft pack --destructive-mode
