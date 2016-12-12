test_files =						\
	firmware.bin					\
	rpi-firmware-20150805.cab

firmware.bin: $(srcdir)/*.elf $(srcdir)/overlays/* $(srcdir)/README
	$(AM_V_GEN) tar -cf $@				\
		$(srcdir)/README			\
		$(srcdir)/*.elf				\
		$(srcdir)/overlays/*.dtb

rpi-firmware-20150805.cab: firmware.bin firmware.inf firmware.metainfo.xml
	$(AM_V_GEN) touch -c -m -d"2000-01-01T00:00:00" $?;	\
	$(GCAB) --create --nopath $@			\
		$(builddir)/firmware.bin		\
		$(srcdir)/firmware.metainfo.xml		\
		$(srcdir)/firmware.inf

BUILT_SOURCES =						\
	firmware.bin					\
	rpi-firmware-20150805.cab

CLEANFILES =						\
	$(BUILT_SOURCES)

EXTRA_DIST =						\
	$(test_files)					\
	firmware.inf					\
	firmware.metainfo.xml				\
	overlays/test.dtb				\
	start.elf

-include $(top_srcdir)/git.mk
