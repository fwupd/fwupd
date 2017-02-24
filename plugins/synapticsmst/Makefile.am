AM_CPPFLAGS = 							\
	$(APPSTREAM_GLIB_CFLAGS)				\
	$(GLIB_CFLAGS)						\
	$(GUSB_CFLAGS)						\
	$(LIBSMBIOS_CFLAGS)					\
	$(VALGRIND_CFLAGS)					\
	$(PIE_CFLAGS)						\
	$(EFIVAR_CFLAGS)					\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/src					\
	-I$(top_srcdir)/libfwupd				\
	-DLIBEXECDIR=\"$(libexecdir)\"				\
	-DLOCALEDIR=\""$(localedir)"\"


FWUPD_LIBS =							\
	$(top_builddir)/libfwupd/libfwupd.la

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_synapticsmst.la

libfu_plugin_synapticsmst_la_SOURCES =				\
	fu-dell-common.h					\
	fu-dell-common.c					\
	synapticsmst-device.c					\
	synapticsmst-device.h					\
	synapticsmst-common.c					\
	synapticsmst-common.h					\
	fu-plugin-synapticsmst.c

libfu_plugin_synapticsmst_la_LIBADD =				\
	$(GUSB_LIBS)						\
	$(LIBSMBIOS_LIBS)					\
	$(GLIB_LIBS)
libfu_plugin_synapticsmst_la_LDFLAGS = -module -avoid-version
libfu_plugin_synapticsmst_la_CFLAGS =				\
	$(PIE_CFLAGS)						\
	$(WARN_CFLAGS)						\
	-DG_LOG_DOMAIN=\"FuPluginSynapticsMST\"

noinst_PROGRAMS =						\
	synapticsmst-tool

synapticsmst_tool_SOURCES =					\
	synapticsmst-common.c					\
	synapticsmst-device.c					\
	synapticsmst-tool.c

synapticsmst_tool_LDADD =					\
	$(LIBSMBIOS_LIBS)					\
	$(EFIVAR_LIBS)						\
	$(APPSTREAM_GLIB_LIBS)					\
	$(GLIB_LIBS)

synapticsmst_tool_CFLAGS = $(AM_CFLAGS) $(WARN_CFLAGS)

check_PROGRAMS =						\
	fu-self-test

fu_self_test_SOURCES =						\
	fu-self-test.c						\
	fu-plugin.c						\
	fu-device.c						\
	fu-pending.c						\
	fu-dell-common.h					\
	fu-dell-common.c					\
	synapticsmst-device.c					\
	synapticsmst-device.h					\
	synapticsmst-common.c					\
	synapticsmst-common.h					\
	fu-plugin-synapticsmst.c

fu_self_test_LDADD =						\
	$(FWUPD_LIBS)						\
	$(GUSB_LIBS)						\
	$(EFIVAR_LIBS)						\
	$(LIBSMBIOS_LIBS)					\
	$(APPSTREAM_GLIB_LIBS)					\
	$(SQLITE_LIBS)						\
	$(GLIB_LIBS)

fu_self_test_LDFLAGS =						\
	$(PIE_LDFLAGS)						\
	$(RELRO_LDFLAGS)

fu_self_test_CFLAGS =						\
	-DFU_OFFLINE_DESTDIR=\"/tmp/fwupd-self-test\"		\
	-DLOCALSTATEDIR=\"/tmp/fwupd-self-test/var\"		\
	$(WARN_CFLAGS)

TESTS = fu-self-test

-include $(top_srcdir)/git.mk
