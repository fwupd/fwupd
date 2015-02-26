# Set ALL_ALL_LINGUAS based on the .po files present. Optional argument is the
# name of the po directory. $podir/LINGUAS.ignore can be used to ignore a
# subset of the po files.

AC_DEFUN([AS_ALL_LINGUAS],
[
 AC_MSG_CHECKING([for linguas])
 podir="m4_default([$1],[$srcdir/po])"
 linguas=`cd $podir && ls *.po 2>/dev/null | awk 'BEGIN { FS="."; ORS=" " } { print $[]1 }'`
 if test -f "$podir/LINGUAS.ignore"; then
   ALL_LINGUAS="";
   ignore_linguas=`sed -n -e 's/^\s\+\|\s\+$//g' -e '/^#/b' -e '/\S/!b' \
                       -e 's/\s\+/\n/g' -e p "$podir/LINGUAS.ignore"`;
   for lang in $linguas; do
     if ! echo "$ignore_linguas" | grep -q "^${lang}$"; then
       ALL_LINGUAS="$ALL_LINGUAS $lang";
     fi;
   done;
 else
   ALL_LINGUAS="$linguas";
 fi;
 AC_SUBST([ALL_LINGUAS])
 AC_MSG_RESULT($ALL_LINGUAS)
])
