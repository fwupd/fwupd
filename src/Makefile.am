introspectiondir = $(datadir)/dbus-1/interfaces
dist_introspection_DATA = 				\
	org.freedesktop.fwupd.xml

AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GUSB_CFLAGS)					\
	$(GCAB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(PIE_CFLAGS)					\
	$(POLKIT_CFLAGS)				\
	$(GPGME_CFLAGS)					\
	$(SOUP_CFLAGS)					\
	$(ARCHIVE_CFLAGS)				\
	$(GUDEV_CFLAGS)					\
	$(VALGRIND_CFLAGS)				\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/libfwupd			\
	-DG_LOG_DOMAIN=\"Fu\"				\
	-DLIBEXECDIR=\"$(libexecdir)\"			\
	-DLIBDIR=\"$(libdir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DVERSION="\"$(VERSION)\""			\
	-DDAEMON_USER="\"$(daemon_user)\""		\
	-DTESTDATADIR=\""$(top_srcdir)/data/tests"\"	\
	-DG_UDEV_API_IS_SUBJECT_TO_CHANGE		\
	-DLOCALEDIR=\""$(localedir)"\"

FWUPD_LIBS =						\
	$(top_builddir)/libfwupd/libfwupd.la

bin_PROGRAMS = fwupdmgr

fwupdmgr_SOURCES =					\
	fu-device.c					\
	fu-device.h					\
	fu-pending.c					\
	fu-pending.h					\
	fu-util.c

fwupdmgr_LDADD =					\
	$(LIBM)						\
	$(FWUPD_LIBS)					\
	$(APPSTREAM_GLIB_LIBS)				\
	$(SQLITE_LIBS)					\
	$(SOUP_LIBS)					\
	$(GUDEV_LIBS)					\
	$(GLIB_LIBS)

fwupdmgr_LDFLAGS =					\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

fwupdmgr_CFLAGS =					\
	-DFU_OFFLINE_DESTDIR=\"\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	$(WARN_CFLAGS)

fwupd-resources.c: fwupd.gresource.xml $(dist_introspection_DATA)
	$(AM_V_GEN)					\
	glib-compile-resources				\
		--sourcedir=$(srcdir)			\
		--sourcedir=$(top_builddir)/data	\
		--target=$@				\
		--generate-source			\
		--c-name fu				\
		$(srcdir)/fwupd.gresource.xml
fwupd-resources.h: fwupd.gresource.xml
	$(AM_V_GEN)					\
	glib-compile-resources				\
		--sourcedir=$(srcdir)			\
		--sourcedir=$(top_builddir)/data	\
		--target=$@				\
		--generate-header			\
		--c-name fu				\
		$(srcdir)/fwupd.gresource.xml

pkglibexec_PROGRAMS =					\
	fwupd

fwupd_SOURCES =						\
	fu-debug.c					\
	fu-debug.h					\
	fu-device.c					\
	fu-device.h					\
	fu-keyring.c					\
	fu-keyring.h					\
	fu-pending.c					\
	fu-pending.h					\
	fu-plugin-private.h				\
	fu-plugin-vfuncs.h				\
	fu-plugin.c					\
	fu-plugin.h					\
	fu-quirks.h					\
	fwupd-resources.c				\
	fwupd-resources.h				\
	fu-main.c

fwupd_LDADD =						\
	$(FWUPD_LIBS)					\
	$(APPSTREAM_GLIB_LIBS)				\
	$(GUSB_LIBS)					\
	$(GCAB_LIBS)					\
	$(GLIB_LIBS)					\
	$(GUDEV_LIBS)					\
	$(POLKIT_LIBS)					\
	$(SQLITE_LIBS)					\
	$(GPGME_LIBS)					\
	$(ARCHIVE_LIBS)

fwupd_LDFLAGS =						\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

fwupd_CFLAGS =						\
	-DFU_OFFLINE_DESTDIR=\"\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	$(WARN_CFLAGS)

TESTS_ENVIRONMENT =					\
	libtool --mode=execute valgrind			\
	--quiet						\
	--leak-check=full				\
	--show-possibly-lost=no

check_PROGRAMS =					\
	fu-self-test

fu_self_test_SOURCES =					\
	fu-device.c					\
	fu-device.h					\
	fu-keyring.c					\
	fu-keyring.h					\
	fu-pending.c					\
	fu-pending.h					\
	fu-plugin.c					\
	fu-plugin.h					\
	fu-self-test.c

fu_self_test_LDADD =					\
	$(LIBM)						\
	$(FWUPD_LIBS)					\
	$(APPSTREAM_GLIB_LIBS)				\
	$(SQLITE_LIBS)					\
	$(GCAB_LIBS)					\
	$(GPGME_LIBS)					\
	$(ARCHIVE_LIBS)					\
	$(GLIB_LIBS)

fu_self_test_LDFLAGS =					\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

fu_self_test_CFLAGS =					\
	-DFU_OFFLINE_DESTDIR=\"/tmp/fwupd-self-test\"	\
	-DLOCALSTATEDIR=\"/tmp/fwupd-self-test/var\"	\
	$(WARN_CFLAGS)

TESTS = fu-self-test

install-data-hook:
	if test -w $$(dirname $(DESTDIR)$(localstatedir)/); then \
		mkdir -p $(DESTDIR)$(localstatedir)/lib/fwupd; \
		chmod 0755 $(DESTDIR)$(localstatedir)/lib/fwupd; \
	fi

BUILT_SOURCES =						\
	fwupd-resources.c				\
	fwupd-resources.h

CLEANFILES = $(BUILT_SOURCES) *.log *.trs

MAINTAINERCLEANFILES =					\
	findings

EXTRA_DIST =						\
	fwupd.gresource.xml

-include $(top_srcdir)/git.mk
