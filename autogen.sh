#!/bin/sh
# Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
#
# Run this to generate all the initial makefiles, etc.
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

test -n "$srcdir" || srcdir=`dirname "$0"`
test -n "$srcdir" || srcdir=.

olddir=`pwd`
cd "$srcdir"

AUTORECONF=`which autoreconf`
if test -z $AUTORECONF; then
        echo "*** No autoreconf found, please install it ***"
        exit 1
fi

autopoint --force
gtkdocize || exit 1
ACLOCAL="${ACLOCAL-aclocal} $ACLOCAL_FLAGS"  AUTOPOINT='intltoolize --automake --copy' autoreconf --force --install --verbose

cd "$olddir"
test -n "$NOCONFIGURE" || "$srcdir/configure" "$@"
