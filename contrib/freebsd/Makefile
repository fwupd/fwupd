# Created by: Norbert Kamiński <norbert.kaminski@3mdeb.com>
# $FreeBSD$

PORTNAME=	fwupd
DISTVERSION=
GH_TAGNAME=
CATEGORIES=	sysutils

MAINTAINER=	norbert.kaminski@3mdeb.com
COMMENT=	Update firmware automatically, safely, and reliably

LICENSE=	LGPL21

BUILD_DEPENDS=	gtkdoc-scan:textproc/gtk-doc \
		help2man:misc/help2man \
		vala:lang/vala \
		${LOCALBASE}/libexec/fwupd/efi/fwupdx64.efi:sysutils/fwupd-efi \
		${PYTHON_PKGNAMEPREFIX}gobject3>0:devel/py-gobject3@${PY_FLAVOR}
LIB_DEPENDS=	libcurl.so:ftp/curl \
		libefiboot.so:devel/libefiboot \
		libgcab-1.0.so:archivers/gcab \
		libgnutls.so:security/gnutls \
		libgpg-error.so:security/libgpg-error \
		libgpgme.so:security/gpgme \
		libgusb.so:devel/libgusb \
		libjcat.so:textproc/libjcat \
		libjson-glib-1.0.so:devel/json-glib \
		libprotobuf-c.so:devel/protobuf-c \
		libxmlb.so:textproc/libxmlb \
		libefiboot.so:devel/gnu-efi

RUN_DEPENDS=	${LOCALBASE}/libexec/fwupd/efi/fwupdx64.efi:sysutils/fwupd-efi

USES=		gnome libarchive meson pkgconfig python:3.8+ shebangfix sqlite
USE_GITHUB=	yes
USE_GNOME=	glib20 introspection:build
GH_ACCOUNT=
INSTALLS_ICONS=	yes
USE_LDCONFIG=	yes

SHEBANG_GLOB=	*.py

MESON_ARGS=	-Dgudev=disabled \
		-Dplugin_amt=false \
		-Dpolkit=disabled \
		-Dsystemd=disabled \
		-Doffline=false \
		-Dtests=false \
		-Ddocs=gtkdoc \
		-Defi_binary=false

.include <bsd.port.mk>
