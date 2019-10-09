CC=afl-gcc ./configure --disable-shared
AFL_HARDEN=1 make
afl-fuzz -m 300 -i fuzzing -o findings ./fu-rom-tool rom @@
