# Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
AC_PREREQ(2.63)

AC_INIT([fwupd],[0.1.0],[http://www.freedesktop.org/software/fwupd/])
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-xz tar-ustar foreign])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_LIBTOOL

m4_ifdef([GOBJECT_INTROSPECTION_CHECK], [GOBJECT_INTROSPECTION_CHECK([0.9.8])])
AM_CONDITIONAL(HAVE_INTROSPECTION, test "x$found_introspection" = xyes)
GLIB_GSETTINGS

# enable nice build output on automake1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

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

dnl ---------------------------------------------------------------------------
dnl - Check library dependencies
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.36.0 gobject-2.0 gthread-2.0 gio-2.0 >= 2.25.9 gio-unix-2.0)
PKG_CHECK_MODULES(GUDEV, gudev-1.0)
PKG_CHECK_MODULES(POLKIT, polkit-gobject-1 >= 0.103)
PKG_CHECK_MODULES(GCAB, libgcab-1.0)
PKG_CHECK_MODULES(APPSTREAM_GLIB, appstream-glib >= 0.3.5)
PKG_CHECK_MODULES(SQLITE, sqlite3)
AC_PATH_PROG(DOCBOOK2MAN, docbook2man)

# ColorHug support
AC_ARG_ENABLE(colorhug, AS_HELP_STRING([--enable-colorhug],[Enable ColorHug support]),
	      enable_colorhug=$enableval, enable_colorhug=yes)
if test x$enable_colorhug != xno; then
	PKG_CHECK_MODULES(COLORHUG, colorhug >= 1.2.9 gusb >= 0.2.2)
	AC_DEFINE(HAVE_COLORHUG,1,[Use ColorHug support])
fi
AM_CONDITIONAL(HAVE_COLORHUG, test x$enable_colorhug = xyes)

# UEFI support
AC_ARG_ENABLE(uefi, AS_HELP_STRING([--enable-uefi],[Enable ColorHug support]),
	      enable_uefi=$enableval, enable_uefi=yes)
if test x$enable_uefi != xno; then
	PKG_CHECK_MODULES(UEFI, fwup)
	AC_DEFINE(HAVE_UEFI,1,[Use ColorHug support])
fi
AM_CONDITIONAL(HAVE_UEFI, test x$enable_uefi = xyes)

# systemd support
AC_ARG_WITH([systemdsystemunitdir],
            AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
            [],
            [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
if test "x$with_systemdsystemunitdir" != "xno"; then
	AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$systemdsystemunitdir"])

dnl ---------------------------------------------------------------------------
dnl - Makefiles, etc.
dnl ---------------------------------------------------------------------------
AC_CONFIG_FILES([
Makefile
man/Makefile
data/Makefile
data/tests/Makefile
policy/Makefile
po/Makefile.in
src/Makefile
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
