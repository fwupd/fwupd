#!/bin/bash
if [ ! $# == 2 ]; then
        echo "Usage : $0 [options] [folder]"
        echo "folder: the folder under fwupd src root path"
        echo "-c: verify that nothing changes when files are processed by script"
        echo "-f: process and replace source files"
        echo "for example: ./contrib/uncrustify/uncrustify.sh  -f plugins/goodix-moc/"
        exit
fi

DIRS=$2
SRCROOT=`git rev-parse --show-toplevel`
CFG="$SRCROOT/contrib/uncrustify/uncrustify.cfg"

echo "dir: $DIRS"

case "$1" in
    -c|--check)
	OPTS="--check"
        ;;
    -f|--folder)
    OPTS="--replace --no-backup"
        ;;
    *)
    echo "Usage : $0 [options] [folder]"
    exit
        ;;
esac

function enumdir(){
    for element in `ls $1`
    do  
        is_dir=$1"/"$element
        if [ -d $is_dir ]
        then 
            enumdir $is_dir
        else
            cfile=`ls "$is_dir" | grep -E '.*\.[ch]$'`
            if [ -n "$cfile" ]
            then
                uncrustify -c "$CFG" $OPTS $cfile
            fi
        fi  
    done
}
enumdir  $SRCROOT/$DIRS
RES=$?

exit $RES
