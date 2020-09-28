#!/bin/bash
SRCROOT=`git rev-parse --show-toplevel`
CFG="$SRCROOT/contrib/uncrustify/uncrustify.cfg"
echo "srcroot: $SRCROOT"

case "$1" in
    -c|--check)
	OPTS="--check"
        ;;
    *)
	OPTS="--replace --no-backup"
        ;;
esac

pushd "$SRCROOT"
## for test purpose, just check plugins/goodixmoc folder
uncrustify -c "$CFG" $OPTS `git ls-tree --name-only -r HEAD | grep -E '.*\.[ch]$' | grep goodixmoc`
RES=$?
popd
exit $RES
