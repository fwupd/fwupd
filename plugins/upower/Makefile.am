AM_CPPFLAGS =						\
	$(GUSB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_upower.la

libfu_plugin_upower_la_SOURCES =			\
	fu-plugin-upower.c
libfu_plugin_upower_la_LIBADD = $(GLIB_LIBS)
libfu_plugin_upower_la_LDFLAGS = -module -avoid-version
libfu_plugin_upower_la_CFLAGS = $(WARN_CFLAGS)		\
	-DG_LOG_DOMAIN=\"FuPluginUpower\"

EXTRA_DIST = README.md

-include $(top_srcdir)/git.mk
