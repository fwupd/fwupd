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

#define FU_ERROR			fu_error_quark()

#define FU_DEVICE_KEY_VERSION		"Version"
#define FU_DEVICE_KEY_PROVIDER		"Provider"
#define FU_DEVICE_KEY_GUID		"Guid"
#define FU_DEVICE_KEY_ONLY_OFFLINE	"OnlyOffline"
#define FU_DEVICE_KEY_DISPLAY_NAME	"DisplayName"
#define FU_DEVICE_KEY_VERSION_NEW	"VersionNew"	/* internal only */
#define FU_DEVICE_KEY_FILENAME_CAB	"FilenameCab"	/* internal only */

typedef enum {
	FU_ERROR_INTERNAL,
	/* private */
	FU_ERROR_LAST
} FuError;

GQuark		 fu_error_quark			(void);

#endif /* __FU_COMMON_H */
