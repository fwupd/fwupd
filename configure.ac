# Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
AC_PREREQ(2.63)

m4_define([fwupd_major_version], [0])
m4_define([fwupd_minor_version], [8])
m4_define([fwupd_micro_version], [2])
m4_define([fwupd_version],
          [fwupd_major_version.fwupd_minor_version.fwupd_micro_version])

AC_INIT([fwupd],[fwupd_version],[https://github.com/hughsie/fwupd])
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-xz tar-ustar foreign subdir-objects])
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
AC_PROG_CC_C99
AC_PROG_INSTALL
LT_INIT
AM_PROG_CC_C_O
IT_PROG_INTLTOOL([0.35.0])
AC_PATH_PROG(XSLTPROC, xsltproc)

WARN_CFLAGS_EXTRA="
	-Waggregate-return
	-Wunused
	-Warray-bounds
	-Wcast-align
	-Wclobbered
	-Wdeclaration-after-statement
	-Wempty-body
	-Wextra
	-Wformat=2
	-Wformat-nonliteral
	-Wformat-security
	-Wformat-signedness
	-Wignored-qualifiers
	-Wimplicit-function-declaration
	-Winit-self
	-Winline
	-Wmissing-declarations
	-Wmissing-format-attribute
	-Wmissing-include-dirs
	-Wmissing-noreturn
	-Wmissing-parameter-type
	-Wmissing-prototypes
	-Wnested-externs
	-Wno-missing-field-initializers
	-Wno-strict-aliasing
	-Wno-suggest-attribute=format
	-Wno-unused-parameter
	-Wold-style-definition
	-Woverride-init
	-Wpointer-arith
	-Wredundant-decls
	-Wreturn-type
	-Wshadow
	-Wsign-compare
	-Wstrict-aliasing
	-Wstrict-prototypes
	-Wswitch-default
	-Wtype-limits
	-Wundef
	-Wuninitialized
	-Wunused-but-set-variable
	-Wwrite-strings"
AX_APPEND_COMPILE_FLAGS([$WARN_CFLAGS_EXTRA], [WARN_CFLAGS])
AC_SUBST(WARN_CFLAGS)

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
PKG_CHECK_MODULES(VALGRIND, valgrind,
		  has_valgrind=yes,
		  has_valgrind=no)
if test x$has_valgrind = xyes; then
	AC_DEFINE(HAVE_VALGRIND,1,
	          [Define as 1 if you have valgrind])
else
	has_valgrind=no
fi

AC_PATH_PROG(DOCBOOK2MAN, docbook2man)
if test -z $DOCBOOK2MAN ; then
	AC_MSG_ERROR([docbook2man program not found])
fi

AC_PATH_PROG(GCAB, [gcab], [no])
if test -z $GCAB ; then
	AC_MSG_ERROR([gcab program not found])
fi

# 0.114 introduced autocleanup functions for its types.
PKG_CHECK_MODULES([POLKIT_0_114], [polkit-gobject-1 >= 0.114],
                  [have_polkit_0_114=yes], [have_polkit_0_114=no])
AS_IF([test "$have_polkit_0_114" = "yes"], [
	AC_DEFINE([HAVE_POLKIT_0_114],[1],
	          [Define as 1 if you have polkit >= 0.114])
])

# ColorHug support
AC_ARG_ENABLE(colorhug,
	      AS_HELP_STRING([--enable-colorhug],
			     [Enable ColorHug support [default=auto]]),,
	      enable_colorhug=maybe)
if test x$enable_colorhug != xno; then
	PKG_CHECK_MODULES(COLORHUG, colorhug >= 1.2.12,
			  has_colorhug=yes,
			  has_colorhug=no)
fi
if test x$has_colorhug = xyes; then
	AC_DEFINE(HAVE_COLORHUG,1,[Use ColorHug support])
else
	has_colorhug=no
	if test "x$enable_colorhug" = "xyes"; then
		AC_MSG_ERROR([colorhug support requested but 'libcolorhug-dev' was not found])
	fi
fi
AM_CONDITIONAL(HAVE_COLORHUG, test x$has_colorhug = xyes)

# Thunderbolt support
AC_ARG_ENABLE(thunderbolt,
	      AS_HELP_STRING([--enable-thunderbolt],
			     [Enable Thunderbolt support [default=auto]]),,
	      enable_thunderbolt=maybe)
if test x$enable_thunderbolt != xno; then
	PKG_CHECK_MODULES(THUNDERBOLT, [libtbtfwu >= 1],
			  has_thunderbolt=yes,
			  has_thunderbolt=no)
fi
if test x$has_thunderbolt = xyes; then
	AC_DEFINE(HAVE_THUNDERBOLT,1,[Use Thunderbolt support])
else
	has_thunderbolt=no
	if test "x$enable_thunderbolt" = "xyes"; then
		AC_MSG_ERROR([thunderbolt support requested but 'libtbtfwu-dev' was not found])
	fi
fi
AM_CONDITIONAL(HAVE_THUNDERBOLT, test x$has_thunderbolt = xyes)

# libelf support
AC_ARG_ENABLE(libelf,
	      AS_HELP_STRING([--enable-libelf],
			     [Enable libelf support [default=auto]]),,
	      enable_libelf=maybe)
if test x$enable_libelf != xno; then
	PKG_CHECK_MODULES(ELF, libelf,
			  has_libelf=yes,
			  has_libelf=no)
fi
if test x$has_libelf = xyes; then
	AC_DEFINE(HAVE_LIBELF,1,[Use libelf support])
else
	has_libelf=no
	if test "x$enable_libelf" = "xyes"; then
		AC_MSG_ERROR([libelf support requested but 'libelf' was not found])
	fi
fi
AM_CONDITIONAL(HAVE_LIBELF, test x$has_libelf = xyes)


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
AC_ARG_ENABLE(uefi,
	      AS_HELP_STRING([--enable-uefi],
	      [Enable UEFI support [default=auto]]),,
	      enable_uefi=maybe)
if test x$enable_uefi != xno; then
	PKG_CHECK_MODULES(UEFI, fwup >= 0.5,
			  has_fwup=yes,
			  has_fwup=no)
fi
if test x$has_fwup = xyes; then
	AC_DEFINE(HAVE_UEFI,1,[Use UEFI support])
	# check for ability to unlock
	PKG_CHECK_MODULES(UEFI_UNLOCK, fwup >= 5,
			  has_uefi_unlock=yes,
			  has_uefi_unlock=no)
	if test x$has_uefi_unlock = xyes; then
		AC_DEFINE(HAVE_UEFI_UNLOCK,1,[Use UEFI unlock support])
	fi
else
	has_fwup=no
	if test "x$enable_uefi" = "xyes"; then
		AC_MSG_ERROR([UEFI support requested but 'libfwup-dev' was not found])
	fi
fi
AM_CONDITIONAL(HAVE_UEFI, test x$has_fwup = xyes)

# Dell Non ESRT capsule support
AC_ARG_ENABLE(dell,
	      AS_HELP_STRING([--enable-dell],
	      [Enable Dell non-ESRT capsule support]),,
	      enable_dell=maybe)
if test x$enable_dell != xno; then
	PKG_CHECK_MODULES(LIBSMBIOS, libsmbios_c >= 2.3.0,
			  has_libsmbios=yes,
			  has_libsmbios=no)
	PKG_CHECK_MODULES(EFIVAR, efivar,
			  has_efivar=yes,
			  has_efivar=no)
fi
if test x$has_libsmbios = xyes &&
   test x$has_efivar = xyes &&
   test x$has_fwup = xyes; then
	AC_DEFINE(HAVE_DELL,1,[Use Dell non-ESRT capsule support])
	has_dell=yes
	# check for ability to change GUID
	PKG_CHECK_MODULES(UEFI_GUID, fwup >= 5,
			  has_uefi_guid=yes,
			  has_uefi_guid=no)
	if test x$has_uefi_guid = xyes; then
		AC_DEFINE(HAVE_UEFI_GUID,1,[Use UEFI GUID override])
	fi
else
	has_dell=no
	if test "x$enable_dell" = "xyes"; then
		if test x$enable_uefi = xno; then
			AC_MSG_ERROR([Dell support requested but UEFI support explicitly disabled])
		fi
		if test x$has_libsmbios = xno; then
			AC_MSG_ERROR([Dell support requested but 'libsmbios-dev' was not found])
		fi
		if test x$has_efivar = xno; then
			AC_MSG_ERROR([Dell support requested but 'libefivar-dev' was not found])
		fi
		if test x$has_fwup = xno; then
			AC_MSG_ERROR([Dell support requested but 'libfwup-dev' was not found])
		fi
	fi
fi
AM_CONDITIONAL(HAVE_DELL, test x$has_dell = xyes)

# Synaptics mst support
AC_ARG_ENABLE(synaptics,
	      AS_HELP_STRING([--enable-synaptics],
	      [Enable Synaptics MST hub support]),,
	      enable_synaptics=maybe)
if test x$enable_synaptics != xno &&
   test x$has_libsmbios = xyes; then
	AC_DEFINE(HAVE_SYNAPTICS,1,[Use Synaptics MST hub  support])
	has_synaptics=yes
else
	has_synaptics=no
	if test "x$enable_synaptics" = "xyes"; then
		if test x$has_libsmbios = xno; then
			AC_MSG_ERROR([Synaptics MST support requested but 'libsmbios-dev' was not found])
		fi
	fi
fi
AM_CONDITIONAL(HAVE_SYNAPTICS, test x$has_synaptics = xyes)

# systemd support
AC_ARG_WITH([systemdunitdir],
            AS_HELP_STRING([--with-systemdunitdir=DIR], [Directory for systemd service files]),
            [],
            [with_systemdunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
AC_SUBST([systemdunitdir], [$with_systemdunitdir])

# EFI system partition location
AC_ARG_WITH([bootdir],
            AS_HELP_STRING([--with-bootdir=DIR], [Directory for EFI system partition]),
            [],
            [with_bootdir=/boot/efi])
AC_SUBST([bootdir], [$with_bootdir])

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
libdfu/tests/Makefile
libfwupd/fwupd-version.h
libfwupd/fwupd.pc
libfwupd/Makefile
data/Makefile
data/pki/Makefile
data/tests/Makefile
data/tests/colorhug/Makefile
docs/Makefile
docs/libdfu/Makefile
docs/libfwupd/Makefile
docs/man/Makefile
policy/Makefile
po/Makefile.in
src/Makefile
plugins/Makefile
plugins/altos/Makefile
plugins/colorhug/Makefile
plugins/dell/Makefile
plugins/dfu/Makefile
plugins/ebitdo/Makefile
plugins/raspberrypi/Makefile
plugins/raspberrypi/rpiupdate/Makefile
plugins/steelseries/Makefile
plugins/synapticsmst/Makefile
plugins/test/Makefile
plugins/thunderbolt/Makefile
plugins/udev/Makefile
plugins/uefi/Makefile
plugins/unifying/Makefile
plugins/upower/Makefile
plugins/usb/Makefile
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

	optional plugins
	------------------
	Colorhug:                  $has_colorhug
	libelf:                    $has_libelf
	UEFI:                      $has_fwup
	Dell:                      $has_dell
	Synaptics MST:             $has_synaptics
	Thunderbolt:               $has_thunderbolt
"
