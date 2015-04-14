/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __FWUPD_ENUMS_H
#define __FWUPD_ENUMS_H

#include <glib.h>

#define FWUPD_DBUS_PATH			"/"
#define FWUPD_DBUS_SERVICE		"org.freedesktop.fwupd"
#define FWUPD_DBUS_INTERFACE		"org.freedesktop.fwupd"

#define FWUPD_DEVICE_ID_ANY		"*"

typedef enum {
	FWUPD_STATUS_IDLE,
	FWUPD_STATUS_LOADING,
	FWUPD_STATUS_DECOMPRESSING,
	FWUPD_STATUS_DEVICE_RESTART,
	FWUPD_STATUS_DEVICE_WRITE,
	FWUPD_STATUS_DEVICE_VERIFY,
	FWUPD_STATUS_SCHEDULING,
	/* private */
	FWUPD_STATUS_LAST
} FwupdStatus;

typedef enum {
	FWUPD_TRUST_FLAG_NONE		= 0,
	FWUPD_TRUST_FLAG_PAYLOAD	= 1,
	FWUPD_TRUST_FLAG_METADATA	= 2,
	/* private */
	FWUPD_TRUST_FLAG_LAST
} FwupdTrustFlags;

const gchar	*fwupd_status_to_string			(FwupdStatus	 status);
FwupdStatus	 fwupd_status_from_string		(const gchar	*status);

#endif /* __FWUPD_ENUMS_H */
