#!/bin/sh
export LD_LIBRARY_PATH="/system/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Android binaries default their temporary directory to /data/local/tmp (see
# Rust's std::env::temp_dir()), which does not exist when running under this
# host-side wrapper -- fall back to a writable host location if unset.
: "${TMPDIR:=/tmp}"
export TMPDIR
exec "$@"
