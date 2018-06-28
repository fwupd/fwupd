#!/bin/sh
set -e

cd po
intltool-update -m
if [ -f missing ]; then
        exit 1
fi
