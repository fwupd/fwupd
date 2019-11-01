Fuzzing
=======

    CC=afl-gcc meson --default-library=static ../
    AFL_HARDEN=1 ninja
    afl-fuzz -m 300 -i fuzzing -o findings ./plugins/dfu/dfu-tool --force dump @@
