if HAVE_INTROSPECTION
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)
endif

AM_CPPFLAGS = \
	$(GLIB_CFLAGS)						\
	$(PIE_CFLAGS)						\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/libfwupd				\
	-DFWUPD_COMPILATION					\
	-DG_LOG_DOMAIN=\"libfwupd\"				\
	-DLIBEXECDIR=\""$(libexecdir)"\"			\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"

lib_LTLIBRARIES =						\
	libfwupd.la

libfwupd_includedir = $(includedir)/fwupd-1
libfwupd_include_HEADERS =					\
	fwupd.h

libfwupdbase_includedir = $(libfwupd_includedir)/libfwupd
libfwupdbase_include_HEADERS =					\
	fwupd-client.h						\
	fwupd-enums.h						\
	fwupd-error.h						\
	fwupd-result.h						\
	fwupd-version.h

libfwupd_la_SOURCES =						\
	fwupd-client.c						\
	fwupd-enums.c						\
	fwupd-enums-private.h					\
	fwupd-error.c						\
	fwupd-result.c
libfwupd_la_LIBADD =						\
	$(GLIB_LIBS)
libfwupd_la_LDFLAGS =						\
	$(PIE_LDFLAGS)						\
	$(RELRO_LDFLAGS)					\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
	-export-dynamic						\
	-no-undefined						\
	-export-symbols-regex '^fwupd_.*'
libfwupd_la_CFLAGS =						\
	$(PIE_CFLAGS)						\
	$(WARN_CFLAGS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = fwupd.pc

EXTRA_DIST =							\
	fwupd-version.h.in					\
	fwupd.pc.in

if HAVE_INTROSPECTION
introspection_sources =						\
	$(libfwupd_la_SOURCES)					\
	$(libfwupdbase_include_HEADERS)

Fwupd-1.0.gir: libfwupd.la
Fwupd_1_0_gir_INCLUDES = GObject-2.0 Gio-2.0
Fwupd_1_0_gir_CFLAGS = $(AM_CPPFLAGS) -DCD_DISABLE_DEPRECATED
Fwupd_1_0_gir_SCANNERFLAGS = --identifier-prefix=Fwupd \
				--symbol-prefix=fwupd \
				--warn-all \
				--add-include-path=$(srcdir) \
				--c-include="fwupd.h"
Fwupd_1_0_gir_EXPORT_PACKAGES = fwupd
Fwupd_1_0_gir_LIBS = libfwupd.la
Fwupd_1_0_gir_FILES = $(introspection_sources)
INTROSPECTION_GIRS += Fwupd-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES = $(gir_DATA) $(typelib_DATA)

endif

TESTS_ENVIRONMENT =						\
	libtool --mode=execute valgrind				\
	--quiet							\
	--leak-check=full					\
	--show-possibly-lost=no

check_PROGRAMS =						\
	fwupd-self-test

fwupd_self_test_SOURCES =					\
	fwupd-client.c						\
	fwupd-enums.c						\
	fwupd-error.c						\
	fwupd-result.c						\
	fwupd-self-test.c

fwupd_self_test_LDADD =						\
	$(LIBM)							\
	$(ARCHIVE_LIBS)						\
	$(GLIB_LIBS)

fwupd_self_test_CFLAGS =					\
	$(WARN_CFLAGS)

TESTS = fwupd-self-test

clean-local:
	rm -f *~

-include $(top_srcdir)/git.mk
