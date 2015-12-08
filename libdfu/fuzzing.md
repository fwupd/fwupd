CC=afl-gcc ./configure --disable-shared
AFL_HARDEN=1 make
afl-fuzz -m 300 -i fuzzing -o findings ./dfu-tool dump @@
