AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(EFIVAR_CFLAGS)				\
	$(GLIB_CFLAGS)					\
	$(GUSB_CFLAGS)					\
	$(LIBSMBIOS_CFLAGS)				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

plugindir = $(libdir)/fwupd-plugins-2
plugin_LTLIBRARIES = libfu_plugin_uefi.la
libfu_plugin_uefi_la_SOURCES =				\
	fu-plugin-uefi.c
libfu_plugin_uefi_la_LIBADD = $(GUSB_LIBS) $(UEFI_LIBS)
libfu_plugin_uefi_la_LDFLAGS = -module -avoid-version
libfu_plugin_uefi_la_CFLAGS = $(WARN_CFLAGS)	\
	-DG_LOG_DOMAIN=\"FuPluginUefi\"

EXTRA_DIST = README.md

-include $(top_srcdir)/git.mk
