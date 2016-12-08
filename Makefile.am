AUTOMAKE_OPTIONS = 1.7

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

SUBDIRS = 						\
	libfwupd					\
	libdfu						\
	po						\
	data						\
	docs						\
	policy						\
	src

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-$(VERSION)-`date +"%Y%m%d"`

EXTRA_DIST =						\
	COPYING						\
	MAINTAINERS					\
	AUTHORS						\
	README.md					\
	NEWS						\
	autogen.sh					\
	config.h					\
	intltool-extract.in				\
	intltool-merge.in				\
	intltool-update.in

MAINTAINERCLEANFILES =					\
	$(srcdir)/INSTALL				\
	$(srcdir)/ABOUT-NLS				\
	$(srcdir)/aclocal.m4				\
	$(srcdir)/autoscan.log				\
	$(srcdir)/compile				\
	$(srcdir)/config.guess				\
	$(srcdir)/config.rpath				\
	$(srcdir)/config.h.in				\
	$(srcdir)/config.sub				\
	$(srcdir)/configure.scan			\
	$(srcdir)/depcomp				\
	$(srcdir)/install-sh				\
	$(srcdir)/ltmain.sh				\
	$(srcdir)/missing				\
	$(srcdir)/mkinstalldirs				\
	$(srcdir)/omf.make				\
	$(srcdir)/test-driver				\
	$(srcdir)/xmldocs.make				\
	$(srcdir)/gtk-doc.make				\
	$(srcdir)/ChangeLog				\
	$(srcdir)/po/Makefile.in.in~			\
	$(srcdir)/fwupd-*.tar.*			\
	`find "$(srcdir)" -type f -name Makefile.in -print`

DISTCHECK_CONFIGURE_FLAGS =				\
	--enable-colorhug				\
	--enable-uefi					\
	--enable-dell					\
	--with-udevrulesdir=$$dc_install_base/$(udevrulesdir) \
	--with-systemdunitdir=$$dc_install_base/$(systemdunitdir)

GITIGNOREFILES =					\
	.tx

distclean-local:
	if test $(srdcir) = .; then :; else \
		rm -f ChangeLog; \
	fi

ChangeLog:
	@echo Creating $@
	@if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git ./missing --run git log e197d80c8937c622f21f569457d1bbd05746d951.. --stat -M -C --name-status  --date=short --no-color) | fmt --split-only > $@.tmp \
	  && mv -f $@.tmp $@ \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git-log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git-log is required to generate this file >> $@); \
	fi

.PHONY: ChangeLog

-include $(top_srcdir)/git.mk
