build:
	true

install:
	sed -i 's!libdir=\(.*\)!libdir=${SNAPCRAFT_STAGE}\1!' ${SNAPCRAFT_STAGE}/lib/pkgconfig/efiboot.pc
	sed -i 's!includedir=\(.*\)!includedir=${SNAPCRAFT_STAGE}\1!' ${SNAPCRAFT_STAGE}/lib/pkgconfig/efiboot.pc
	sed -i 's!Cflags:\(.*\)!Cflags:\1 -L$$\{libdir\}!' ${SNAPCRAFT_STAGE}/lib/pkgconfig/efiboot.pc
	sed -i 's!libdir=\(.*\)!libdir=${SNAPCRAFT_STAGE}\1!' ${SNAPCRAFT_STAGE}/lib/pkgconfig/efivar.pc
	sed -i 's!includedir=\(.*\)!includedir=${SNAPCRAFT_STAGE}\1!' ${SNAPCRAFT_STAGE}/lib/pkgconfig/efivar.pc
	sed -i 's!Cflags:\(.*\)!Cflags:\1 -L$$\{libdir\}!' ${SNAPCRAFT_STAGE}/lib/pkgconfig/efivar.pc
