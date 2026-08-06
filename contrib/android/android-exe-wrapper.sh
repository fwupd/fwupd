#!/bin/sh
export LD_LIBRARY_PATH="/system/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$@"
