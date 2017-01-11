AM_CPPFLAGS = 							\
	$(APPSTREAM_GLIB_CFLAGS)				\
	$(GLIB_CFLAGS)						\
	$(GUSB_CFLAGS)						\
	$(LIBSMBIOS_CFLAGS)                             	\
	$(PIE_CFLAGS)						\
	$(EFIVAR_CFLAGS)					\
	-I$(top_srcdir)/src					\
	-I$(top_srcdir)/libfwupd				\
	-DLOCALEDIR=\""$(localedir)"\"


plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_synapticsmst.la

libfu_plugin_synapticsmst_la_SOURCES =				\
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
	$(lib_LTLIBRARIES)					\
	$(LIBSMBIOS_LIBS)					\
	$(APPSTREAM_GLIB_LIBS)					\
	$(GLIB_LIBS)						\
	$(LIBM)

synapticsmst_tool_CFLAGS = $(AM_CFLAGS) $(WARN_CFLAGS)

-include $(top_srcdir)/git.mk
