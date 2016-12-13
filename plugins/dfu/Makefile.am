AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GUSB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

DFU_LIBS =						\
	$(top_builddir)/libdfu/libdfu.la

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_dfu.la

libfu_plugin_dfu_la_SOURCES =				\
	fu-plugin-dfu.c
libfu_plugin_dfu_la_LIBADD = 				\
	$(GUSB_LIBS)					\
	$(DFU_LIBS)

libfu_plugin_dfu_la_LDFLAGS = -module -avoid-version
libfu_plugin_dfu_la_CFLAGS = $(WARN_CFLAGS)		\
	-DG_LOG_DOMAIN=\"FuPluginDfu\"

EXTRA_DIST = README.md

-include $(top_srcdir)/git.mk
