AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GUSB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_unifying.la

libfu_plugin_unifying_la_SOURCES =			\
	fu-plugin-unifying.c				\
	unifying-dongle.c				\
	unifying-dongle.h
libfu_plugin_unifying_la_LIBADD = $(GUSB_LIBS)
libfu_plugin_unifying_la_LDFLAGS = -module -avoid-version
libfu_plugin_unifying_la_CFLAGS = $(WARN_CFLAGS)	\
	-DG_LOG_DOMAIN=\"FuPluginDfu\"

EXTRA_DIST = README.md

noinst_PROGRAMS =					\
	unifying-tool

unifying_tool_SOURCES =					\
	unifying-dongle.c				\
	unifying-dongle.h				\
	unifying-tool.c

unifying_tool_LDADD =					\
	$(lib_LTLIBRARIES)				\
	$(APPSTREAM_GLIB_LIBS)				\
	$(GLIB_LIBS)					\
	$(GUSB_LIBS)					\
	$(LIBM)

unifying_tool_CFLAGS = $(AM_CFLAGS) $(WARN_CFLAGS)

-include $(top_srcdir)/git.mk
