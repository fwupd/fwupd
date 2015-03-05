introspectiondir = $(datadir)/dbus-1/interfaces
dist_introspection_DATA = 				\
	org.freedesktop.fwupd.xml

AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(COLORHUG_CFLAGS)				\
	$(GCAB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(GUDEV_CFLAGS)					\
	$(PIE_CFLAGS)					\
	$(POLKIT_CFLAGS)				\
	$(UEFI_CFLAGS)					\
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
	fu-common.c					\
	fu-common.h					\
	fu-device.c					\
	fu-device.h					\
	fu-pending.c					\
	fu-pending.h					\
	fu-util.c

fwupdmgr_LDADD =					\
	$(APPSTREAM_GLIB_LIBS)				\
	$(SQLITE_LIBS)					\
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
	fu-common.c					\
	fu-common.h					\
	fu-debug.c					\
	fu-debug.h					\
	fu-device.c					\
	fu-device.h					\
	fu-pending.c					\
	fu-pending.h					\
	fu-provider.c					\
	fu-provider.h					\
	fu-resources.c					\
	fu-resources.h					\
	fu-main.c

if HAVE_COLORHUG
fwupd_SOURCES +=					\
	fu-provider-chug.c				\
	fu-provider-chug.h
endif

if HAVE_UEFI
fwupd_SOURCES +=					\
	fu-provider-uefi.c				\
	fu-provider-uefi.h
endif

fwupd_LDADD =						\
	$(APPSTREAM_GLIB_LIBS)				\
	$(COLORHUG_LIBS)				\
	$(GCAB_LIBS)					\
	$(GLIB_LIBS)					\
	$(GUDEV_LIBS)					\
	$(POLKIT_LIBS)					\
	$(SQLITE_LIBS)					\
	$(UEFI_LIBS)

fwupd_LDFLAGS =						\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

fwupd_CFLAGS =						\
	$(WARNINGFLAGS_C)


TESTS_ENVIRONMENT =					\
	libtool --mode=execute valgrind			\
	--quiet						\
	--leak-check=full				\
	--show-possibly-lost=no

check_PROGRAMS =					\
	fu-self-test

fu_self_test_SOURCES =					\
	fu-common.c					\
	fu-common.h					\
	fu-device.c					\
	fu-device.h					\
	fu-pending.c					\
	fu-pending.h					\
	fu-self-test.c

fu_self_test_LDADD =					\
	$(SQLITE_LIBS)					\
	$(GLIB_LIBS)

fu_self_test_CFLAGS = $(WARNINGFLAGS_C)

install-data-hook:
	if test -w $(DESTDIR)$(prefix)/; then \
		mkdir -p $(DESTDIR)$(localstatedir)/lib/fwupd; \
		chmod 0755 $(DESTDIR)$(localstatedir)/lib/fwupd; \
	fi

TESTS = fu-self-test

BUILT_SOURCES =						\
	fu-resources.c					\
	fu-resources.h

CLEANFILES = $(BUILT_SOURCES) *.log *.trs

EXTRA_DIST =						\
	fwupd.gresource.xml

-include $(top_srcdir)/git.mk
