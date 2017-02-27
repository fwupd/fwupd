SUBDIRS = tests pki

configdir = $(sysconfdir)
dist_config_DATA = fwupd.conf

dbusdir = $(sysconfdir)/dbus-1/system.d
dist_dbus_DATA = org.freedesktop.fwupd.conf

appstreamdir = $(datadir)/app-info/xmls
dist_appstream_DATA = org.freedesktop.fwupd.xml

dbusservicemaindir = $(datadir)/dbus-1/system-services
dbusservicemain_in_files = org.freedesktop.fwupd.service.in
dbusservicemain_DATA     = $(dbusservicemain_in_files:.service.in=.service)
$(dbusservicemain_DATA): $(dbusservicemain_in_files) Makefile
	@sed -e "s|\@servicedir\@|$(libexecdir)|" $< | \
	sed -e "s|\@daemon_user\@|$(daemon_user)|" > $@

%.service: %.service.in Makefile
	$(AM_V_GEN)sed -e 's|\@servicedir\@|$(libexecdir)|' \
	               -e 's|\@localstatedir\@|$(localstatedir)|' \
	               -e 's|\@bootdir\@|$(bootdir)|' \
	               -e 's|\@bindir\@|$(bindir)|' \
	               -e 's|\@daemon_user\@|$(daemon_user)|' $< > $@.tmp && mv $@.tmp $@

systemdservicedir = $(systemdunitdir)
systemdservice_in_files =				\
	fwupd.service.in				\
	fwupd-offline-update.service.in
systemdservice_DATA = $(systemdservice_in_files:.service.in=.service)

install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(systemdunitdir)/system-update.target.wants
	ln -sf ../fwupd-offline-update.service $(DESTDIR)$(systemdunitdir)/system-update.target.wants/fwupd-offline-update.service

udevrules_DATA =					\
	90-fwupd-devices.rules

EXTRA_DIST =						\
	$(udevrules_DATA)				\
	$(dbusservicemain_in_files)			\
	$(systemdservice_in_files)

DISTCLEANFILES =					\
	$(dbusservicemain_DATA)				\
	$(systemdservice_DATA)

-include $(top_srcdir)/git.mk
