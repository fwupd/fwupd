AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GLIB_CFLAGS)					\
	$(GUDEV_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	$(PIE_CFLAGS)					\
	-DTESTDATADIR=\""$(top_srcdir)/plugins/udev"\"	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

FWUPD_LIBS =						\
	$(top_builddir)/libfwupd/libfwupd.la

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_udev.la

libfu_plugin_udev_la_SOURCES =				\
	fu-rom.c					\
	fu-rom.h					\
	fu-plugin-udev.c
libfu_plugin_udev_la_LIBADD = 				\
	$(GUSB_LIBS)					\
	$(GUDEV_LIBS)
libfu_plugin_udev_la_LDFLAGS = -module -avoid-version
libfu_plugin_udev_la_CFLAGS = $(WARN_CFLAGS)		\
	-DG_LOG_DOMAIN=\"FuPluginUdev\"

noinst_PROGRAMS = fu-rom-tool

fu_rom_tool_SOURCES =					\
	fu-rom.c					\
	fu-rom.h					\
	fu-rom-tool.c

fu_rom_tool_LDADD =					\
	$(LIBM)						\
	$(FWUPD_LIBS)					\
	$(APPSTREAM_GLIB_LIBS)				\
	$(SQLITE_LIBS)					\
	$(SOUP_LIBS)					\
	$(GUDEV_LIBS)					\
	$(GLIB_LIBS)

fu_rom_tool_LDFLAGS =					\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

fu_rom_tool_CFLAGS =					\
	-DFU_OFFLINE_DESTDIR=\"\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	$(WARN_CFLAGS)

check_PROGRAMS =					\
	fu-self-test

fu_self_test_SOURCES =					\
	fu-rom.c					\
	fu-rom.h					\
	fu-self-test.c

fu_self_test_LDADD =					\
	$(LIBM)						\
	$(FWUPD_LIBS)					\
	$(APPSTREAM_GLIB_LIBS)				\
	$(GLIB_LIBS)

fu_self_test_LDFLAGS =					\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

fu_self_test_CFLAGS =					\
	-DFU_OFFLINE_DESTDIR=\"/tmp/fwupd-self-test\"	\
	-DLOCALSTATEDIR=\"/tmp/fwupd-self-test/var\"	\
	$(WARN_CFLAGS)

TESTS = fu-self-test

MAINTAINERCLEANFILES =					\
	findings

EXTRA_DIST = README.md

-include $(top_srcdir)/git.mk
