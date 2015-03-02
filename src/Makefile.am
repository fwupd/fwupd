introspectiondir = $(datadir)/dbus-1/interfaces
dist_introspection_DATA = 				\
	org.freedesktop.fwupd.xml

AM_CPPFLAGS =						\
	$(PIE_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(POLKIT_CFLAGS)				\
	$(GUDEV_CFLAGS)					\
	-DG_LOG_DOMAIN=\"Fu\"				\
	-DLIBEXECDIR=\"$(libexecdir)\"			\
	-DLIBDIR=\"$(libdir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	-DVERSION="\"$(VERSION)\""			\
	-DDAEMON_USER="\"$(daemon_user)\""		\
	-DG_UDEV_API_IS_SUBJECT_TO_CHANGE		\
	-DLOCALEDIR=\""$(localedir)"\"

bin_PROGRAMS = fwupdmgr

fwupdmgr_SOURCES =					\
	fu-device.c					\
	fu-device.h					\
	fu-util.c

fwupdmgr_LDADD =					\
	$(GLIB_LIBS)

fwupdmgr_LDFLAGS =					\
	$(PIE_LDFLAGS)

fwupdmgr_CFLAGS =					\
	$(WARNINGFLAGS_C)

fu-resources.c: fwupd.gresource.xml $(dist_introspection_DATA)
	$(AM_V_GEN)					\
	glib-compile-resources				\
		--sourcedir=$(srcdir)			\
		--sourcedir=$(top_builddir)/data	\
		--target=$@				\
		--generate-source			\
		--c-name fu				\
		$(srcdir)/fwupd.gresource.xml
fu-resources.h: fwupd.gresource.xml
	$(AM_V_GEN)					\
	glib-compile-resources				\
		--sourcedir=$(srcdir)			\
		--sourcedir=$(top_builddir)/data	\
		--target=$@				\
		--generate-header			\
		--c-name fu				\
		$(srcdir)/fwupd.gresource.xml

libexec_PROGRAMS =					\
	fwupd

fwupd_SOURCES =						\
	fu-cleanup.h					\
	fu-common.h					\
	fu-debug.c					\
	fu-debug.h					\
	fu-device.c					\
	fu-device.h					\
	fu-provider.c					\
	fu-provider.h					\
	fu-provider-uefi.c				\
	fu-provider-uefi.h				\
	fu-resources.c					\
	fu-resources.h					\
	fu-main.c

fwupd_LDADD =						\
	$(POLKIT_LIBS)					\
	$(GUDEV_LIBS)					\
	$(GLIB_LIBS)

fwupd_LDFLAGS =						\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

fwupd_CFLAGS =						\
	$(WARNINGFLAGS_C)

BUILT_SOURCES =						\
	fu-resources.c					\
	fu-resources.h

CLEANFILES = $(BUILT_SOURCES) *.log *.trs

EXTRA_DIST =						\
	fwupd.gresource.xml

-include $(top_srcdir)/git.mk
