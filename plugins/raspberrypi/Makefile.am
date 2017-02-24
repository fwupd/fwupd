SUBDIRS = 						\
	rpiupdate

AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GUSB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(VALGRIND_CFLAGS)				\
	$(PIE_CFLAGS)					\
	-DLIBEXECDIR=\"$(libexecdir)\"			\
	-DTESTDATADIR=\""$(top_srcdir)/plugins/raspberrypi"\" \
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

FWUPD_LIBS =						\
	$(top_builddir)/libfwupd/libfwupd.la

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_raspberrypi.la

libfu_plugin_raspberrypi_la_SOURCES =			\
	fu-plugin-raspberrypi.c
libfu_plugin_raspberrypi_la_LIBADD = $(GUSB_LIBS)
libfu_plugin_raspberrypi_la_LDFLAGS = -module -avoid-version
libfu_plugin_raspberrypi_la_CFLAGS = $(WARN_CFLAGS)	\
	-DG_LOG_DOMAIN=\"FuPluginRpi\"

test_files =						\
	rpiboot/start.elf

EXTRA_DIST = $(test_files)

TESTS_ENVIRONMENT =					\
	libtool --mode=execute valgrind			\
	--quiet						\
	--leak-check=full				\
	--show-possibly-lost=no

check_PROGRAMS =					\
	fu-self-test

fu_self_test_SOURCES =					\
	fu-plugin.c					\
	fu-pending.c					\
	fu-device.c					\
	fu-plugin-raspberrypi.c				\
	fu-plugin-raspberrypi.h				\
	fu-self-test.c

fu_self_test_LDADD =					\
	$(LIBM)						\
	$(FWUPD_LIBS)					\
	$(APPSTREAM_GLIB_LIBS)				\
	$(ARCHIVE_LIBS)					\
	$(SQLITE_LIBS)					\
	$(GLIB_LIBS)

fu_self_test_LDFLAGS =					\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

fu_self_test_CFLAGS =					\
	-DFU_OFFLINE_DESTDIR=\"/tmp/fwupd-self-test\"	\
	-DLOCALSTATEDIR=\"/tmp/fwupd-self-test/var\"	\
	$(WARN_CFLAGS)

TESTS = fu-self-test

EXTRA_DIST += README.md

-include $(top_srcdir)/git.mk
