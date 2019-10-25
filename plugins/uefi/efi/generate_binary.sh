#!/bin/sh
output=$2
objcopy_cmd=$(which objcopy)
genpeimg_cmd=$(which genpeimg)

$objcopy_cmd  -j .text \
              -j .sdata \
              -j .data \
              -j .dynamic \
              -j .dynsym \
              -j .rel \
              -j .rela \
              -j .reloc \
              $*

if [ -n "${genpeimg_cmd}" ]; then
        $genpeimg_cmd -d \
                      +d \
                      -d \
                      +n \
                      -d \
                      +s \
                      $output
fi
