AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GLIB_CFLAGS)					\
	$(GUDEV_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	$(THUNDERBOLT_CFLAGS)				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_thunderbolt.la
libfu_plugin_thunderbolt_la_SOURCES =			\
	fu-plugin-thunderbolt.c
libfu_plugin_thunderbolt_la_LIBADD = $(GUDEV_LIBS) $(THUNDERBOLT_LIBS)
libfu_plugin_thunderbolt_la_LDFLAGS = -module -avoid-version
libfu_plugin_thunderbolt_la_CFLAGS = $(WARN_CFLAGS)	\
	-DG_LOG_DOMAIN=\"FuPluginThunderbolt\"

if HAVE_DELL
AM_CPPFLAGS +=						\
	$(EFIVAR_CFLAGS)				\
	$(LIBSMBIOS_CFLAGS)
libfu_plugin_thunderbolt_la_LIBADD +=			\
	$(LIBSMBIOS_LIBS)
libfu_plugin_thunderbolt_la_SOURCES +=			\
	fu-dell-common.h				\
	fu-dell-common.c
endif

EXTRA_DIST = README.md

-include $(top_srcdir)/git.mk
