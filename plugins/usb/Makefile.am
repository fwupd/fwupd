AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GLIB_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_usb.la

libfu_plugin_usb_la_SOURCES =				\
	fu-plugin-usb.c
libfu_plugin_usb_la_LIBADD = $(GUSB_LIBS)
libfu_plugin_usb_la_LDFLAGS = -module -avoid-version
libfu_plugin_usb_la_CFLAGS = $(WARN_CFLAGS)		\
	-DG_LOG_DOMAIN=\"FuPluginUsb\"

EXTRA_DIST = README.md

-include $(top_srcdir)/git.mk
