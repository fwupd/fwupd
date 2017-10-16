Fuzzing
=======

    CC=afl-gcc meson --default-library=static ../
    AFL_HARDEN=1 ninja
    afl-fuzz -m 300 -i ../src/fuzzing/smbios -o src/smbios/findings ./src/fwupdmgr smbios-dump @@
