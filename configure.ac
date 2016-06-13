# Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
AC_PREREQ(2.63)

m4_define([fwupd_major_version], [0])
m4_define([fwupd_minor_version], [7])
m4_define([fwupd_micro_version], [3])
m4_define([fwupd_version],
          [fwupd_major_version.fwupd_minor_version.fwupd_micro_version])

AC_INIT([fwupd],[fwupd_version],[https://github.com/hughsie/fwupd])
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-xz tar-ustar foreign])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_LIBTOOL

m4_ifdef([GOBJECT_INTROSPECTION_CHECK], [GOBJECT_INTROSPECTION_CHECK([0.9.8])])
AM_CONDITIONAL(HAVE_INTROSPECTION, test "x$found_introspection" = xyes)
GLIB_GSETTINGS

# use this in fu-version.h
FWUPD_MAJOR_VERSION=fwupd_major_version
FWUPD_MINOR_VERSION=fwupd_minor_version
FWUPD_MICRO_VERSION=fwupd_micro_version
AC_SUBST(VERSION)
AC_SUBST(FWUPD_MAJOR_VERSION)
AC_SUBST(FWUPD_MINOR_VERSION)
AC_SUBST(FWUPD_MICRO_VERSION)

# libtool versioning - this applies to libfwupd
#
# See http://sources.redhat.com/autobook/autobook/autobook_91.html#SEC91 for details
#
# increment;
# CURRENT	If the API or ABI interface has changed (reset REVISION to 0)
# REVISION	If the API and ABI remains the same, but bugs are fixed.
# AGE		Don't use.
LT_CURRENT=1
LT_REVISION=1
LT_AGE=0
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)

# enable nice build output on automake1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

# check for gtk-doc
GTK_DOC_CHECK([1.14],[--flavour no-tmpl])

AS_ALL_LINGUAS
AC_PROG_CC
AC_PROG_INSTALL
LT_INIT
AM_PROG_CC_C_O
IT_PROG_INTLTOOL([0.35.0])
AC_PATH_PROG(XSLTPROC, xsltproc)

dnl ---------------------------------------------------------------------------
dnl - Extra verbose warning switches
dnl ---------------------------------------------------------------------------

if test "$GCC" = "yes"; then
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wall"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wcast-align -Wno-uninitialized"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-declarations"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wpointer-arith"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wcast-align"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wwrite-strings"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Winit-self"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wreturn-type"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wformat-nonliteral"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wformat-security"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-include-dirs"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-format-attribute"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wclobbered"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wempty-body"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wignored-qualifiers"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wsign-compare"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wtype-limits"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wuninitialized"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Waggregate-return"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wdeclaration-after-statement"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wshadow"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wno-strict-aliasing"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Winline"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Wmissing-parameter-type"
	WARNINGFLAGS_C="$WARNINGFLAGS_C -Woverride-init"
else
	WARNINGFLAGS_C=""
fi
AC_SUBST(WARNINGFLAGS_C)

dnl ---------------------------------------------------------------------------
dnl - gettext stuff
dnl ---------------------------------------------------------------------------
AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])

GETTEXT_PACKAGE=AC_PACKAGE_NAME
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE(GETTEXT_PACKAGE, "AC_PACKAGE_NAME", [foo])

# check for PIE (position independent executable) support
if test x$with_pic != xno; then
	AX_CHECK_COMPILE_FLAG([-fPIE],
			      [AX_CHECK_LINK_FLAG([-fPIE -pie],
						  [PIE_CFLAGS="-fPIE" PIE_LDFLAGS="-pie"])])
	AC_SUBST(PIE_CFLAGS)
	AC_SUBST(PIE_LDFLAGS)
fi

# check for full RELRO (relocation read-only) support
AX_CHECK_LINK_FLAG([-Wl,-z,relro,-z,now],
		   [RELRO_LDFLAGS="-Wl,-z,relro,-z,now"])
AC_SUBST([RELRO_LDFLAGS])

# use -lm
LT_LIB_M
AC_SUBST(LIBM)

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.45.8 gobject-2.0 gthread-2.0 gio-2.0 >= 2.25.9 gio-unix-2.0 gmodule-2.0)
PKG_CHECK_MODULES(GUDEV, gudev-1.0)
PKG_CHECK_MODULES(POLKIT, polkit-gobject-1 >= 0.103)
PKG_CHECK_MODULES(GCAB, libgcab-1.0)
PKG_CHECK_MODULES(APPSTREAM_GLIB, appstream-glib >= 0.5.10)
PKG_CHECK_MODULES(GUSB, gusb >= 0.2.9)
PKG_CHECK_MODULES(SQLITE, sqlite3)
PKG_CHECK_MODULES(ARCHIVE, libarchive)
PKG_CHECK_MODULES(SOUP, libsoup-2.4 >= 2.51.92)
AC_PATH_PROG(DOCBOOK2MAN, docbook2man)
if test -z $DOCBOOK2MAN ; then
	AC_MSG_ERROR([docbook2man program not found])
fi

AC_PATH_PROG(GCAB, [gcab], [no])
if test -z $GCAB ; then
	AC_MSG_ERROR([gcab program not found])
fi

# ColorHug support
AC_ARG_ENABLE(colorhug, AS_HELP_STRING([--enable-colorhug],[Enable ColorHug support]),
	      enable_colorhug=$enableval, enable_colorhug=yes)
if test x$enable_colorhug != xno; then
	PKG_CHECK_MODULES(COLORHUG, colorhug >= 1.2.12)
	AC_DEFINE(HAVE_COLORHUG,1,[Use ColorHug support])
fi
AM_CONDITIONAL(HAVE_COLORHUG, test x$enable_colorhug = xyes)

# gpgme support
AC_MSG_CHECKING([for gpgme])
if ! test -x "/usr/bin/gpgme-config"; then
	AC_MSG_ERROR([Cannot locate gpgme])
else
	AC_MSG_RESULT([yes])
	GPGME_CFLAGS="`\"/usr/bin/gpgme-config\" --cflags`"
	GPGME_LIBS="`\"/usr/bin/gpgme-config\" --libs`"
	GPGME_CFLAGS+=" `\"/usr/bin/gpg-error-config\" --cflags`"
	GPGME_LIBS+=" `\"/usr/bin/gpg-error-config\" --libs`"
	AC_SUBST([GPGME_CFLAGS])
	AC_SUBST([GPGME_LIBS])
fi

# UEFI support
AC_ARG_ENABLE(uefi, AS_HELP_STRING([--enable-uefi],[Enable UEFI support]),
	      enable_uefi=$enableval, enable_uefi=yes)
if test x$enable_uefi != xno; then
	PKG_CHECK_MODULES(UEFI, fwup >= 0.5)
	AC_DEFINE(HAVE_UEFI,1,[Use UEFI support])

	# check for ability to unlock
	PKG_CHECK_MODULES(UEFI_UNLOCK, fwup >= 0.6,
			  has_uefi_unlock=yes,
			  has_uefi_unlock=no)
	if test x$has_uefi_unlock = xyes; then
		AC_DEFINE(HAVE_UEFI_UNLOCK,1,[Use UEFI unlock support])
	fi
fi
AM_CONDITIONAL(HAVE_UEFI, test x$enable_uefi = xyes)

# systemd support
AC_ARG_WITH([systemdunitdir],
            AS_HELP_STRING([--with-systemdunitdir=DIR], [Directory for systemd service files]),
            [],
            [with_systemdunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
AC_SUBST([systemdunitdir], [$with_systemdunitdir])

# udev rules
AC_ARG_WITH([udevrulesdir],
            AS_HELP_STRING([--with-udevrulesdir=DIR], [Directory for udev rules files]),
            [],
            [with_udevrulesdir=$($PKG_CONFIG --variable=udevdir udev)/rules.d])
AC_SUBST([udevrulesdir], [$with_udevrulesdir])

dnl ---------------------------------------------------------------------------
dnl - Makefiles, etc.
dnl ---------------------------------------------------------------------------
AC_CONFIG_FILES([
Makefile
libdfu/Makefile
libdfu/dfu.pc
libfwupd/fwupd-version.h
libfwupd/fwupd.pc
libfwupd/Makefile
data/Makefile
data/pki/Makefile
data/tests/Makefile
data/tests/rpiupdate/Makefile
data/tests/colorhug/Makefile
data/tests/dfu/Makefile
docs/Makefile
docs/libdfu/Makefile
docs/libfwupd/Makefile
docs/man/Makefile
policy/Makefile
po/Makefile.in
src/Makefile
src/plugins/Makefile
])
AC_OUTPUT

dnl ==========================================================================
echo "
                    fwupd $VERSION
                  =================

        prefix:                    ${prefix}
        datadir:                   ${datadir}
        compiler:                  ${CC}
        cflags:                    ${CFLAGS}
        cppflags:                  ${CPPFLAGS}
"
