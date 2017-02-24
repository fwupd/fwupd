AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GUSB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(COLORHUG_CFLAGS)				\
	$(LIBSMBIOS_CFLAGS)				\
	$(EFIVAR_CFLAGS)				\
	$(GUDEV_CFLAGS)					\
	$(VALGRIND_CFLAGS)				\
	$(PIE_CFLAGS)					\
	-DLIBEXECDIR=\"$(libexecdir)\"			\
	-DTESTDATADIR=\""$(top_srcdir)/data/tests"\"	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

FWUPD_LIBS =						\
	$(top_builddir)/libfwupd/libfwupd.la

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_dell.la
libfu_plugin_dell_la_SOURCES =				\
	fu-dell-common.c					\
	fu-plugin-dell.c
libfu_plugin_dell_la_LIBADD =				\
	$(GUSB_LIBS)					\
	$(UEFI_LIBS)					\
	$(LIBSMBIOS_LIBS)
libfu_plugin_dell_la_LDFLAGS = -module -avoid-version
libfu_plugin_dell_la_CFLAGS = $(WARN_CFLAGS)		\
	-DG_LOG_DOMAIN=\"FuPluginDell\"

check_PROGRAMS =					\
	fu-self-test

fu_self_test_SOURCES =					\
	fu-plugin.c					\
	fu-pending.c					\
	fu-device.c					\
	fu-dell-common.c				\
	fu-dell-common.h				\
	fu-plugin-dell.c				\
	fu-plugin-dell.h				\
	fu-self-test.c

fu_self_test_LDADD =					\
	$(APPSTREAM_GLIB_LIBS)				\
	$(ARCHIVE_LIBS)					\
	$(FWUPD_LIBS)					\
	$(GLIB_LIBS)					\
	$(GUSB_LIBS)					\
	$(LIBM)						\
	$(LIBSMBIOS_LIBS)				\
	$(SQLITE_LIBS)					\
	$(UEFI_LIBS)

fu_self_test_LDFLAGS =					\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

fu_self_test_CFLAGS =					\
	-DFU_OFFLINE_DESTDIR=\"/tmp/fwupd-self-test\"	\
	-DLOCALSTATEDIR=\"/tmp/fwupd-self-test/var\"	\
	$(WARN_CFLAGS)

noinst_PROGRAMS =                                       \
        fu-dell-tool

fu_dell_tool_SOURCES =					\
	fu-dell-common.c				\
	fu-dell-common.h				\
	fu-dell-tool.c

fu_dell_tool_LDADD =					\
	$(GLIB_LIBS)					\
	$(FWUPD_LIBS)					\
	$(EFIVAR_LIBS)					\
	$(LIBSMBIOS_LIBS)

fu_dell_tool_LDFLAGS =					\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

fu_dell_tool_CFLAGS =					\
	$(WARN_CFLAGS)


TESTS = fu-self-test

EXTRA_DIST = README.md

-include $(top_srcdir)/git.mk
