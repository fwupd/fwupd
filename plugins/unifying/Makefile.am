AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GUSB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

FWUPD_LIBS =						\
	$(top_builddir)/libfwupd/libfwupd.la

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_unifying.la

libfu_plugin_unifying_la_SOURCES =			\
	fu-device-unifying.c				\
	fu-device-unifying.h				\
	fu-plugin-unifying.c
libfu_plugin_unifying_la_LIBADD = $(GUSB_LIBS)
libfu_plugin_unifying_la_LDFLAGS = -module -avoid-version
libfu_plugin_unifying_la_CFLAGS = $(WARN_CFLAGS)	\
	-DG_LOG_DOMAIN=\"FuPluginUnifying\"

EXTRA_DIST = README.md

noinst_PROGRAMS =					\
	fu-unifying-tool

fu_unifying_tool_SOURCES =				\
	fu-device-unifying.c				\
	fu-device-unifying.h				\
	fu-device.c					\
	fu-device.h					\
	fu-unifying-tool.c

fu_unifying_tool_LDADD =				\
	$(lib_LTLIBRARIES)				\
	$(APPSTREAM_GLIB_LIBS)				\
	$(FWUPD_LIBS)					\
	$(GLIB_LIBS)					\
	$(GUSB_LIBS)					\
	$(LIBM)

unifying_tool_CFLAGS = $(AM_CFLAGS) $(WARN_CFLAGS)

-include $(top_srcdir)/git.mk
