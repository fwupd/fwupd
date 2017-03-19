AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GUDEV_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

FWUPD_LIBS =						\
	$(top_builddir)/libfwupd/libfwupd.la

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_unifying.la

libunify_common =					\
	lu-common.c					\
	lu-common.h					\
	lu-context.c					\
	lu-context.h					\
	lu-device-bootloader.c				\
	lu-device-bootloader.h				\
	lu-device-bootloader-nordic.c			\
	lu-device-bootloader-nordic.h			\
	lu-device-bootloader-texas.c			\
	lu-device-bootloader-texas.h			\
	lu-device.c					\
	lu-device.h					\
	lu-device-runtime.c				\
	lu-device-runtime.h				\
	lu-hidpp.h

libfu_plugin_unifying_la_SOURCES =			\
	$(libunify_common)				\
	fu-plugin-unifying.c
libfu_plugin_unifying_la_LIBADD = $(GUSB_LIBS) $(GUDEV_LIBS)
libfu_plugin_unifying_la_LDFLAGS = -module -avoid-version
libfu_plugin_unifying_la_CFLAGS = $(WARN_CFLAGS)	\
	-DG_LOG_DOMAIN=\"FuPluginUnifying\"

EXTRA_DIST = README.md

noinst_PROGRAMS =					\
	unifying-tool

unifying_tool_SOURCES =					\
	$(libunify_common)				\
	fu-device.c					\
	fu-device.h					\
	lu-tool.c

unifying_tool_LDADD =					\
	$(lib_LTLIBRARIES)				\
	$(APPSTREAM_GLIB_LIBS)				\
	$(FWUPD_LIBS)					\
	$(GLIB_LIBS)					\
	$(GUDEV_LIBS)					\
	$(GUSB_LIBS)					\
	$(LIBM)

unifying_tool_CFLAGS = $(AM_CFLAGS) $(WARN_CFLAGS)	\
	-DG_LOG_DOMAIN=\"LuTool\"

-include $(top_srcdir)/git.mk
