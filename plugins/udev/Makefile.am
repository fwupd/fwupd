AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GLIB_CFLAGS)					\
	$(GUDEV_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_udev.la

libfu_plugin_udev_la_SOURCES =				\
	fu-plugin-udev.c
libfu_plugin_udev_la_LIBADD = 				\
	$(GUSB_LIBS)					\
	$(GUDEV_LIBS)
libfu_plugin_udev_la_LDFLAGS = -module -avoid-version
libfu_plugin_udev_la_CFLAGS = $(WARN_CFLAGS)		\
	-DG_LOG_DOMAIN=\"FuPluginUdev\"

EXTRA_DIST = README.md

-include $(top_srcdir)/git.mk
