AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GLIB_CFLAGS)					\
	$(GUDEV_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

DFU_LIBS =						\
	$(top_builddir)/libdfu/libdfu.la

FWUPD_LIBS =						\
	$(top_builddir)/libfwupd/libfwupd.la

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_altos.la
libfu_plugin_altos_la_SOURCES =				\
	fu-device-altos.c				\
	fu-device-altos.h				\
	fu-plugin-altos.c
libfu_plugin_altos_la_LIBADD =				\
	$(GUDEV_LIBS)					\
	$(DFU_LIBS)
libfu_plugin_altos_la_LDFLAGS = -module -avoid-version
libfu_plugin_altos_la_CFLAGS = $(WARN_CFLAGS)		\
	-DG_LOG_DOMAIN=\"FuPluginAltos\"

noinst_PROGRAMS =					\
	fu-altos-tool

fu_altos_tool_SOURCES =					\
	fu-device-altos.c				\
	fu-device-altos.h				\
	fu-device.c					\
	fu-device.h					\
	fu-altos-tool.c

fu_altos_tool_LDADD =					\
	$(APPSTREAM_GLIB_LIBS)				\
	$(DFU_LIBS)					\
	$(FWUPD_LIBS)					\
	$(GLIB_LIBS)					\
	$(GUDEV_LIBS)					\
	$(GUSB_LIBS)					\
	$(LIBM)

fu_altos_tool_CFLAGS = -DEGG_TEST $(AM_CFLAGS) $(WARN_CFLAGS)

EXTRA_DIST = README.md

-include $(top_srcdir)/git.mk
