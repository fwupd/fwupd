test_files =								\
	colorhug-als-3.0.2.cab

colorhug-als-3.0.2.cab: firmware.bin firmware.bin.asc firmware.metainfo.xml
	$(AM_V_GEN) touch -c -m -d"2000-01-01T00:00:00" $?;		\
	$(GCAB) --create --nopath $@					\
		$(srcdir)/firmware.bin					\
		$(srcdir)/firmware.bin.asc				\
		$(srcdir)/firmware.metainfo.xml

BUILT_SOURCES =								\
	colorhug-als-3.0.2.cab

CLEANFILES =								\
	$(BUILT_SOURCES)

EXTRA_DIST =								\
	$(test_files)							\
	firmware.bin							\
	firmware.bin.asc						\
	firmware.metainfo.xml

-include $(top_srcdir)/git.mk
