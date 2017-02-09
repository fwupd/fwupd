AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GUSB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(COLORHUG_CFLAGS)				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_colorhug.la
libfu_plugin_colorhug_la_SOURCES =			\
	fu-plugin-colorhug.c
libfu_plugin_colorhug_la_LIBADD = $(GUSB_LIBS) $(COLORHUG_LIBS)
libfu_plugin_colorhug_la_LDFLAGS = -module -avoid-version
libfu_plugin_colorhug_la_CFLAGS = $(WARN_CFLAGS)	\
	-DG_LOG_DOMAIN=\"FuPluginColorHug\"

EXTRA_DIST = README.md

-include $(top_srcdir)/git.mk
