#!/usr/bin/env bash

adb -e shell mkdir -p ${2}

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
 -O -c -C "${1}" . | dd status=progress | adb -e shell "tar -z -x -C ${2} -f -"


exit 0
# Previous attempt at incremental updates using adb push

CUR_DIR="$(pwd)"
IN_PATH="$(realpath ${1})"

for f in ${IN_PATH}/**/* ; do

	#echo "f ${f}"
  OUT_FILE="$(realpath --relative-to="${IN_PATH}" ${f})"
  #echo "in ${IN_FILE}"
  PARENT="$(dirname $OUT_FILE)"
  REL_PATH="${PARENT##${1}/}"
	#echo "parent ${PARENT}"
	#echo "rel ${REL_PATH}"

  adb -e push --sync "${f}" "${2}/${PARENT}"
done
