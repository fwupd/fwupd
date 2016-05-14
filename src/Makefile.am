SUBDIRS = plugins

introspectiondir = $(datadir)/dbus-1/interfaces
dist_introspection_DATA = 				\
	org.freedesktop.fwupd.xml

AM_CPPFLAGS =						\
	$(APPSTREAM_GLIB_CFLAGS)			\
	$(GUSB_CFLAGS)					\
	$(GCAB_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(GUDEV_CFLAGS)					\
	$(PIE_CFLAGS)					\
	$(POLKIT_CFLAGS)				\
	$(GPGME_CFLAGS)					\
	$(SOUP_CFLAGS)					\
	$(ARCHIVE_CFLAGS)				\
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

DFU_LIBS =						\
	$(top_builddir)/libdfu/libdfu.la

bin_PROGRAMS = fwupdmgr

fwupdmgr_SOURCES =					\
	fu-device.c					\
	fu-device.h					\
	fu-pending.c					\
	fu-pending.h					\
	fu-rom.c					\
	fu-rom.h					\
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
	$(PIE_LDFLAGS)

fwupdmgr_CFLAGS =					\
	-DFU_OFFLINE_DESTDIR=\"\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
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
	fu-plugin.c					\
	fu-plugin.h					\
	fu-provider.c					\
	fu-provider.h					\
	fu-provider-dfu.c				\
	fu-provider-dfu.h				\
	fu-provider-rpi.c				\
	fu-provider-rpi.h				\
	fu-provider-udev.c				\
	fu-provider-udev.h				\
	fu-provider-usb.c				\
	fu-provider-usb.h				\
	fu-quirks.h					\
	fu-resources.c					\
	fu-resources.h					\
	fu-rom.c					\
	fu-rom.h					\
	fu-main.c

fwupd_LDADD =						\
	$(FWUPD_LIBS)					\
	$(DFU_LIBS)					\
	$(APPSTREAM_GLIB_LIBS)				\
	$(GUSB_LIBS)					\
	$(GCAB_LIBS)					\
	$(GLIB_LIBS)					\
	$(GUDEV_LIBS)					\
	$(POLKIT_LIBS)					\
	$(SQLITE_LIBS)					\
	$(GPGME_LIBS)					\
	$(ARCHIVE_LIBS)

if HAVE_COLORHUG
fwupd_SOURCES +=					\
	fu-provider-chug.c				\
	fu-provider-chug.h
fwupd_LDADD +=						\
	$(COLORHUG_LIBS)
AM_CPPFLAGS +=						\
	$(COLORHUG_CFLAGS)
endif

if HAVE_UEFI
fwupd_SOURCES +=					\
	fu-provider-uefi.c				\
	fu-provider-uefi.h
AM_CPPFLAGS +=						\
	$(UEFI_CFLAGS)
fwupd_LDADD +=						\
	$(UEFI_LIBS)
endif

fwupd_LDFLAGS =						\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

fwupd_CFLAGS =						\
	-DFU_OFFLINE_DESTDIR=\"\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	$(WARNINGFLAGS_C)

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
	fu-provider.c					\
	fu-provider.h					\
	fu-provider-fake.c				\
	fu-provider-fake.h				\
	fu-provider-rpi.c				\
	fu-provider-rpi.h				\
	fu-rom.c					\
	fu-rom.h					\
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

fu_self_test_CFLAGS =					\
	-DFU_OFFLINE_DESTDIR=\"/tmp/fwupd-self-test\"	\
	-DLOCALSTATEDIR=\"/tmp/fwupd-self-test/var\"	\
	$(WARNINGFLAGS_C)

if HAVE_DELL
fwupd_SOURCES +=					\
	fu-provider-dell.c				\
	fu-provider-dell.h
AM_CPPFLAGS +=						\
	$(LIBSMBIOS_CFLAGS)				\
	$(EFIVAR_CFLAGS)
fwupd_LDADD +=						\
	$(LIBSMBIOS_LIBS)				\
	$(EFIVAR_LIBS)
fu_self_test_LDADD +=					\
	$(GUSB_LIBS)					\
	$(UEFI_LIBS)					\
	$(LIBSMBIOS_LIBS)				\
	$(EFIVAR_LIBS)
fu_self_test_SOURCES +=					\
	fu-provider-dell.c				\
	fu-provider-dell.h
endif

install-data-hook:
	if test -w $(DESTDIR)$(prefix)/; then \
		mkdir -p $(DESTDIR)$(localstatedir)/lib/fwupd; \
		chmod 0755 $(DESTDIR)$(localstatedir)/lib/fwupd; \
		mkdir -p $(DESTDIR)$(localstatedir)/cache/app-info/xmls; \
		mkdir -p $(DESTDIR)$(localstatedir)/cache/app-info/icons; \
	fi

TESTS = fu-self-test

BUILT_SOURCES =						\
	fu-resources.c					\
	fu-resources.h

CLEANFILES = $(BUILT_SOURCES) *.log *.trs

EXTRA_DIST =						\
	fwupd.gresource.xml

-include $(top_srcdir)/git.mk
