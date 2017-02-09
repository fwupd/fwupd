AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GUSB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_steelseries.la

libfu_plugin_steelseries_la_SOURCES =			\
	fu-plugin-steelseries.c
libfu_plugin_steelseries_la_LIBADD = $(GUSB_LIBS)
libfu_plugin_steelseries_la_LDFLAGS = -module -avoid-version
libfu_plugin_steelseries_la_CFLAGS = $(WARN_CFLAGS)	\
	-DG_LOG_DOMAIN=\"FuPluginSteelSeries\"

EXTRA_DIST = README.md

-include $(top_srcdir)/git.mk
