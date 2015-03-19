/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FU_COMMON_H
#define __FU_COMMON_H

#define FWUPD_DBUS_PATH			"/"
#define FWUPD_DBUS_SERVICE		"org.freedesktop.fwupd"
#define FWUPD_DBUS_INTERFACE		"org.freedesktop.fwupd"

#define FWUPD_DEVICE_ID_ANY		"*"

#define FU_DEVICE_KEY_VERSION		"Version"	/* s */
#define FU_DEVICE_KEY_PROVIDER		"Provider"	/* s */
#define FU_DEVICE_KEY_GUID		"Guid"		/* s */
#define FU_DEVICE_KEY_ONLY_OFFLINE	"OnlyOffline"	/* s */
#define FU_DEVICE_KEY_DISPLAY_NAME	"DisplayName"	/* s */
#define FU_DEVICE_KEY_VERSION_NEW	"VersionNew"	/* internal only */
#define FU_DEVICE_KEY_VERSION_OLD	"VersionOld"	/* internal only */
#define FU_DEVICE_KEY_FILENAME_CAB	"FilenameCab"	/* internal only */
#define FU_DEVICE_KEY_VERSION_LOWEST	"VersionLowest"	/* s */
#define FU_DEVICE_KEY_VENDOR		"Vendor"	/* s */
#define FU_DEVICE_KEY_NAME		"Name"		/* s */
#define FU_DEVICE_KEY_SUMMARY		"Summary"	/* s */
#define FU_DEVICE_KEY_DESCRIPTION	"Description"	/* s */
#define FU_DEVICE_KEY_LICENSE		"License"	/* s */
#define FU_DEVICE_KEY_KIND		"Kind"		/* s: 'internal' or 'hotplug' */
#define FU_DEVICE_KEY_URL_HOMEPAGE	"UrlHomepage"	/* s */
#define FU_DEVICE_KEY_SIZE		"Size"		/* t */
#define FU_DEVICE_KEY_PENDING_STATE	"PendingState"	/* s */
#define FU_DEVICE_KEY_PENDING_ERROR	"PendingError"	/* s */

typedef enum {
	FU_STATUS_IDLE,
	FU_STATUS_LOADING,
	FU_STATUS_DECOMPRESSING,
	FU_STATUS_DEVICE_RESTART,
	FU_STATUS_DEVICE_WRITE,
	FU_STATUS_DEVICE_VERIFY,
	FU_STATUS_SCHEDULING,
	/* private */
	FU_STATUS_LAST
} FuStatus;

const gchar	*fu_status_to_string			(FuStatus	 status);
FuStatus	 fu_status_from_string			(const gchar	*status);

#endif /* __FU_COMMON_H */
