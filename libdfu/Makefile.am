AM_CPPFLAGS = \
	$(GLIB_CFLAGS)						\
	$(GUSB_CFLAGS)						\
	-I$(top_srcdir)/libdfu					\
	-I$(top_srcdir)/libfwupd				\
	-I$(top_srcdir)						\
	-I$(top_builddir)					\
	-DG_USB_API_IS_SUBJECT_TO_CHANGE			\
	-DG_LOG_DOMAIN=\"libdfu\"				\
	-DTESTDATADIR=\""$(top_srcdir)/data/tests/dfu"\"	\
	-DLOCALEDIR=\""$(localedir)"\"

FWUPD_LIBS =							\
	$(top_builddir)/libfwupd/libfwupd.la

privlibdir=$(libdir)/fwupd
privlib_LTLIBRARIES =						\
	libdfu-private.la

libdfu_private_la_SOURCES =					\
	dfu.h							\
	dfu-common.c						\
	dfu-common.h						\
	dfu-device.c						\
	dfu-device.h						\
	dfu-device-private.h					\
	dfu-element.c						\
	dfu-element.h						\
	dfu-element-private.h					\
	dfu-firmware.c						\
	dfu-firmware.h						\
	dfu-image.c						\
	dfu-image.h						\
	dfu-image-private.h					\
	dfu-target.c						\
	dfu-target.h						\
	dfu-target-private.h

libdfu_private_la_LIBADD =					\
	$(FWUPD_LIBS)						\
	$(GUSB_LIBS)						\
	$(GLIB_LIBS)

libdfu_private_la_LDFLAGS =					\
	-export-dynamic						\
	-no-undefined						\
	-avoid-version						\
	-export-symbols-regex '^dfu_.*'

libdfu_private_la_CFLAGS =					\
	$(WARNINGFLAGS_C)

bin_PROGRAMS =							\
	dfu-tool

dfu_tool_SOURCES =						\
	dfu-tool.c

dfu_tool_LDADD =						\
	$(privlib_LTLIBRARIES)					\
	$(GLIB_LIBS)						\
	$(GUSB_LIBS)

dfu_tool_CFLAGS = -DEGG_TEST $(AM_CFLAGS) $(WARNINGFLAGS_C)

TESTS_ENVIRONMENT =						\
	libtool --mode=execute valgrind				\
	--quiet							\
	--leak-check=full					\
	--show-possibly-lost=no

check_PROGRAMS =						\
	dfu-self-test

dfu_self_test_SOURCES =						\
	dfu-self-test.c

dfu_self_test_LDADD =						\
	$(privlib_LTLIBRARIES)					\
	$(GLIB_LIBS)						\
	$(GUSB_LIBS)

dfu_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS) $(WARNINGFLAGS_C)

TESTS = dfu-self-test

CLEANFILES = *.log *.trs $(BUILT_SOURCES)

MAINTAINERCLEANFILES = *.dfu *.bin

clean-local:
	rm -f *~

-include $(top_srcdir)/git.mk
