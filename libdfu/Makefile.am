SUBDIRS = tests

if HAVE_INTROSPECTION
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)
endif

AM_CPPFLAGS = \
	$(APPSTREAM_GLIB_CFLAGS)				\
	$(GLIB_CFLAGS)						\
	$(GUSB_CFLAGS)						\
	$(ELF_CFLAGS)						\
	$(PIE_CFLAGS)						\
	-I$(top_srcdir)/libdfu					\
	-I$(top_srcdir)						\
	-I$(top_builddir)					\
	-DG_USB_API_IS_SUBJECT_TO_CHANGE			\
	-DG_LOG_DOMAIN=\"Dfu\"					\
	-DTESTDATADIR=\""$(top_srcdir)/libdfu/tests"\"		\
	-DLOCALEDIR=\""$(localedir)"\"

lib_LTLIBRARIES =						\
	libdfu.la

libdfu_includedir = $(includedir)
libdfu_include_HEADERS =					\
	dfu.h

libdfubase_includedir = $(libdfu_includedir)/libdfu
libdfubase_include_HEADERS =					\
	dfu-common.h						\
	dfu-context.h						\
	dfu-device.h						\
	dfu-element.h						\
	dfu-error.h						\
	dfu-firmware.h						\
	dfu-image.h						\
	dfu-sector.h						\
	dfu-target.h

libdfu_la_SOURCES =						\
	dfu.h							\
	dfu-common.c						\
	dfu-common.h						\
	dfu-cipher-devo.c					\
	dfu-cipher-devo.h					\
	dfu-cipher-xtea.c					\
	dfu-cipher-xtea.h					\
	dfu-context.c						\
	dfu-context.h						\
	dfu-device.c						\
	dfu-device.h						\
	dfu-device-private.h					\
	dfu-element.c						\
	dfu-element.h						\
	dfu-error.c						\
	dfu-error.h						\
	dfu-firmware.c						\
	dfu-firmware.h						\
	dfu-firmware-private.h					\
	dfu-format-dfu.c					\
	dfu-format-dfu.h					\
	dfu-format-dfuse.c					\
	dfu-format-dfuse.h					\
	dfu-format-elf.c					\
	dfu-format-elf.h					\
	dfu-format-ihex.c					\
	dfu-format-ihex.h					\
	dfu-format-metadata.c					\
	dfu-format-metadata.h					\
	dfu-format-raw.c					\
	dfu-format-raw.h					\
	dfu-image.c						\
	dfu-image.h						\
	dfu-sector.c						\
	dfu-sector.h						\
	dfu-sector-private.h					\
	dfu-target.c						\
	dfu-target.h						\
	dfu-target-private.h

libdfu_la_LIBADD =						\
	$(APPSTREAM_GLIB_LIBS)					\
	$(ELF_LIBS)						\
	$(GUSB_LIBS)						\
	$(GLIB_LIBS)						\
	$(LIBM)

libdfu_la_LDFLAGS =						\
	$(PIE_LDFLAGS)						\
	$(RELRO_LDFLAGS)					\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
	-export-dynamic						\
	-no-undefined						\
	-export-symbols-regex '^dfu_.*'

libdfu_la_CFLAGS =						\
	$(PIE_CFLAGS)						\
	$(WARN_CFLAGS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = dfu.pc

EXTRA_DIST =							\
	dfu.pc.in

bin_PROGRAMS =							\
	dfu-tool

dfu_tool_SOURCES =						\
	dfu-progress-bar.c					\
	dfu-progress-bar.h					\
	dfu-tool.c

dfu_tool_LDADD =						\
	$(lib_LTLIBRARIES)					\
	$(APPSTREAM_GLIB_LIBS)					\
	$(ELF_LIBS)						\
	$(GLIB_LIBS)						\
	$(GUSB_LIBS)						\
	$(LIBM)

dfu_tool_CFLAGS = -DEGG_TEST $(AM_CFLAGS) $(WARN_CFLAGS)

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
	$(lib_LTLIBRARIES)					\
	$(APPSTREAM_GLIB_LIBS)					\
	$(GLIB_LIBS)						\
	$(GUSB_LIBS)

dfu_self_test_LDFLAGS =						\
	$(PIE_LDFLAGS)						\
	$(RELRO_LDFLAGS)

dfu_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS) $(WARN_CFLAGS)

TESTS = dfu-self-test

CLEANFILES = *.log *.trs $(BUILT_SOURCES)

MAINTAINERCLEANFILES = *.dfu *.bin
if HAVE_INTROSPECTION
introspection_sources =						\
	$(libdfu_la_SOURCES)

Dfu-1.0.gir: libdfu.la
Dfu_1_0_gir_INCLUDES = GObject-2.0 Gio-2.0 GUsb-1.0 AppStreamGlib-1.0
Dfu_1_0_gir_CFLAGS = $(AM_CPPFLAGS)
Dfu_1_0_gir_LDFLAGS = $(ELF_LIBS)
Dfu_1_0_gir_SCANNERFLAGS = --identifier-prefix=Dfu \
				--symbol-prefix=dfu \
				--warn-all \
				--add-include-path=$(srcdir) \
				--c-include="dfu.h"
Dfu_1_0_gir_EXPORT_PACKAGES = dfu
Dfu_1_0_gir_LIBS = libdfu.la
Dfu_1_0_gir_FILES = $(introspection_sources)
INTROSPECTION_GIRS += Dfu-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA) *.log *.trs *.test
endif

clean-local:
	rm -f *~

-include $(top_srcdir)/git.mk
