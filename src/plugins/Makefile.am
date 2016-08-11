
AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GUSB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/libebitdo			\
	-I$(top_srcdir)/libfwupd			\
	-I$(top_srcdir)/src

plugindir = $(libdir)/fwupd-plugins-1
plugin_LTLIBRARIES =					\
	libfu_plugin_ebitdo.la				\
	libfu_plugin_steelseries.la			\
	libfu_plugin_test.la

libfu_plugin_test_la_SOURCES =				\
	fu-plugin-test.c
libfu_plugin_test_la_LIBADD = $(GLIB_LIBS)
libfu_plugin_test_la_LDFLAGS = -module -avoid-version
libfu_plugin_test_la_CFLAGS = $(WARN_CFLAGS)		\
	-DG_LOG_DOMAIN=\"FuPluginTest\"

EBITDO_LIBS =						\
	$(top_builddir)/libebitdo/libebitdo.la

libfu_plugin_ebitdo_la_SOURCES =			\
	fu-plugin-ebitdo.c
libfu_plugin_ebitdo_la_LIBADD = $(GUSB_LIBS) $(EBITDO_LIBS)
libfu_plugin_ebitdo_la_LDFLAGS = -module -avoid-version
libfu_plugin_ebitdo_la_CFLAGS = $(WARN_CFLAGS)		\
	-DG_LOG_DOMAIN=\"FuPluginEbitdo\"

libfu_plugin_steelseries_la_SOURCES =			\
	fu-plugin-steelseries.c
libfu_plugin_steelseries_la_LIBADD = $(GUSB_LIBS)
libfu_plugin_steelseries_la_LDFLAGS = -module -avoid-version
libfu_plugin_steelseries_la_CFLAGS = $(WARN_CFLAGS)	\
	-DG_LOG_DOMAIN=\"FuPluginSteelSeries\"

-include $(top_srcdir)/git.mk
