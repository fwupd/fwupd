#!/usr/bin/env bash
set -euo pipefail

# Assume we're wireless debugging
ADB_FLAGS="${ADB_FLAGS=-e}"

adb ${ADB_FLAGS} shell mkdir -p ${2}

# This is about the same speed as adb push but actually replaces files
# After excludes it is faster
# Unfortunately toybox tar doesn't support --keep-newer-files
# Can I use --listed-incremental to only push changed files?
time tar -z \
    --exclude="share/man" \
    --exclude="share/locale" \
    --exclude="share/installed-tests" \
    --exclude="share/gdb" \
    --exclude="share/fish" \
    --exclude="share/dbus-1" \
    --exclude="share/bash-completion" \
    --exclude="share/aclocal" \
    --exclude="libexec/installed-tests" \
    --exclude="lib/pkgconfig" \
    --exclude="lib/glib-2.0" \
    --exclude="lib/gio" \
    --exclude="include" \
    --exclude="bin/g*" \
    --exclude="bin/json-glib*" \
    -O -c -C "${1}" . | dd status=progress | adb ${ADB_FLAGS} shell "tar -z -x -C ${2} -f -"
