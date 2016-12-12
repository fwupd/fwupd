AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GUSB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(GUDEV_CFLAGS)					\
	$(PIE_CFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_ebitdo.la

libfu_plugin_ebitdo_la_SOURCES =			\
	fu-ebitdo-common.c				\
	fu-ebitdo-common.h				\
	fu-ebitdo-device.c				\
	fu-ebitdo-device.h				\
	fu-plugin-ebitdo.c
libfu_plugin_ebitdo_la_LIBADD = $(GUSB_LIBS)
libfu_plugin_ebitdo_la_LDFLAGS = -module -avoid-version
libfu_plugin_ebitdo_la_CFLAGS = $(WARN_CFLAGS)		\
	-DG_LOG_DOMAIN=\"FuPluginEbitdo\"

noinst_PROGRAMS =					\
	fu-ebitdo-tool

fu_ebitdo_tool_SOURCES =				\
	fu-ebitdo-common.c				\
	fu-ebitdo-common.h				\
	fu-ebitdo-device.c				\
	fu-ebitdo-device.h				\
	fu-ebitdo-tool.c

fu_ebitdo_tool_LDADD =					\
	$(lib_LTLIBRARIES)				\
	$(APPSTREAM_GLIB_LIBS)				\
	$(GLIB_LIBS)					\
	$(GUSB_LIBS)					\
	$(LIBM)

fu_ebitdo_tool_CFLAGS = -DEGG_TEST $(AM_CFLAGS) $(WARN_CFLAGS)

EXTRA_DIST = README.md

-include $(top_srcdir)/git.mk
