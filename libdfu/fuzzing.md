Fuzzing
=======

    CC=afl-gcc meson --default-library=static ../
    AFL_HARDEN=1 ninja
    afl-fuzz -m 300 -i fuzzing -o findings ./libdfu/dfu-tool --force dump @@
    afl-fuzz -m 300 -i fuzzing-patch-dump -o findings ./libdfu/dfu-tool --force patch-dump @@

Generating
----------

    mkdir -p fuzzing-patch-dump
    echo -n hello > complete-replace.old
    echo -n XXXXX > complete-replace.new
    ./libdfu/dfu-tool patch-create complete-replace.old complete-replace.new fuzzing-patch-dump/complete-replace.bdiff

    echo -n helloworldhelloworldhelloworldhelloworld > grow-two-chunks.old
    echo -n XelloXorldhelloworldhelloworldhelloworlXXX > grow-two-chunks.new
    ./libdfu/dfu-tool patch-create grow-two-chunks.old grow-two-chunks.new fuzzing-patch-dump/grow-two-chunks.bdiff
