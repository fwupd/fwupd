AM_CPPFLAGS = \
	$(APPSTREAM_GLIB_CFLAGS)				\
	$(GLIB_CFLAGS)						\
	$(GUSB_CFLAGS)						\
	$(PIE_CFLAGS)						\
	-I$(top_srcdir)/libebitdo				\
	-I$(top_srcdir)						\
	-I$(top_builddir)					\
	-DG_USB_API_IS_SUBJECT_TO_CHANGE			\
	-DG_LOG_DOMAIN=\"libebitdo\"				\
	-DTESTDATADIR=\""$(top_srcdir)/data/tests/ebitdo"\"	\
	-DLOCALEDIR=\""$(localedir)"\"

lib_LTLIBRARIES =						\
	libebitdo.la

libebitdo_includedir = $(includedir)
libebitdo_include_HEADERS =					\
	ebitdo.h

libebitdobase_includedir = $(libebitdo_includedir)/libebitdo
libebitdobase_include_HEADERS =					\
	ebitdo-device.h

libebitdo_la_SOURCES =						\
	ebitdo.h						\
	ebitdo-common.c						\
	ebitdo-common.h						\
	ebitdo-device.c						\
	ebitdo-device.h

libebitdo_la_LIBADD =						\
	$(APPSTREAM_GLIB_LIBS)					\
	$(GUSB_LIBS)						\
	$(GLIB_LIBS)

libebitdo_la_LDFLAGS =						\
	$(PIE_LDFLAGS)						\
	$(RELRO_LDFLAGS)					\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
	-export-dynamic						\
	-no-undefined						\
	-export-symbols-regex '^ebitdo_.*'

libebitdo_la_CFLAGS =						\
	$(PIE_CFLAGS)						\
	$(WARN_CFLAGS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = ebitdo.pc

EXTRA_DIST =							\
	ebitdo.pc.in

bin_PROGRAMS =							\
	ebitdo-tool

ebitdo_tool_SOURCES =						\
	ebitdo-tool.c

ebitdo_tool_LDADD =						\
	$(lib_LTLIBRARIES)					\
	$(APPSTREAM_GLIB_LIBS)					\
	$(GLIB_LIBS)						\
	$(GUSB_LIBS)						\
	$(LIBM)

ebitdo_tool_CFLAGS = -DEGG_TEST $(AM_CFLAGS) $(WARN_CFLAGS)

clean-local:
	rm -f *~

-include $(top_srcdir)/git.mk
