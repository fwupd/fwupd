SUBDIRS = tests pki

dbusdir = $(sysconfdir)/dbus-1/system.d
dist_dbus_DATA = org.freedesktop.fwupd.conf

dbusservicemaindir = $(datadir)/dbus-1/system-services
dbusservicemain_in_files = org.freedesktop.fwupd.service.in
dbusservicemain_DATA     = $(dbusservicemain_in_files:.service.in=.service)
$(dbusservicemain_DATA): $(dbusservicemain_in_files) Makefile
	@sed -e "s|\@servicedir\@|$(libexecdir)|" $< | \
	sed -e "s|\@daemon_user\@|$(daemon_user)|" > $@

EXTRA_DIST =						\
	$(dbusservicemain_in_files)

if HAVE_SYSTEMD
systemdservicemain_in_files = fwupd.service.in
systemdservicemaindir = $(systemdsystemunitdir)
systemdservicemain_DATA     = $(systemdservicemain_in_files:.service.in=.service)
$(systemdservicemain_DATA): $(systemdservicemain_in_files) Makefile
	@sed -e "s|\@servicedir\@|$(libexecdir)|" $< | \
	sed -e "s|\@daemon_user\@|$(daemon_user)|" > $@
EXTRA_DIST += $(systemdservicemain_in_files)
endif

DISTCLEANFILES =					\
	$(dbusservicemain_DATA)				\
	$(systemdservicemain_DATA)

-include $(top_srcdir)/git.mk
