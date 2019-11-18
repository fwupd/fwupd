#!/bin/sh
output=$2
objcopy_cmd=$(command -v objcopy)
genpeimg_cmd=$(command -v genpeimg)

"$objcopy_cmd"  -j .text \
              -j .sdata \
              -j .data \
              -j .dynamic \
              -j .dynsym \
              -j '.rel*' \
              "$@"

if [ -n "${genpeimg_cmd}" ]; then
        $genpeimg_cmd -d \
                      +d \
                      -d \
                      +n \
                      -d \
                      +s \
                      "$output"
fi
