/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#ifndef __FWUPD_ENUMS_PRIVATE_H
#define __FWUPD_ENUMS_PRIVATE_H

/* FIXME: change the keys to the new names when we bump major version */
#define FWUPD_RESULT_KEY_DEVICE_CREATED		"Created"	/* t */
#define FWUPD_RESULT_KEY_DEVICE_DESCRIPTION	"Description"	/* s */
#define FWUPD_RESULT_KEY_DEVICE_FLAGS		"Flags"		/* t */
#define FWUPD_RESULT_KEY_DEVICE_CHECKSUM	"FirmwareHash"	/* s */
#define FWUPD_RESULT_KEY_DEVICE_CHECKSUM_KIND	"DeviceChecksumKind"	/* u */
#define FWUPD_RESULT_KEY_DEVICE_MODIFIED	"Modified"	/* t */
#define FWUPD_RESULT_KEY_DEVICE_NAME		"DisplayName"	/* s */
#define FWUPD_RESULT_KEY_DEVICE_ID		"DeviceID"	/* s */
#define FWUPD_RESULT_KEY_DEVICE_PLUGIN		"Plugin"	/* s */
#define FWUPD_RESULT_KEY_DEVICE_VERSION		"Version"	/* s */
#define FWUPD_RESULT_KEY_DEVICE_VERSION_LOWEST	"VersionLowest"	/* s */
#define FWUPD_RESULT_KEY_DEVICE_VERSION_BOOTLOADER	"VersionBootloader"	/* s */
#define FWUPD_RESULT_KEY_DEVICE_FLASHES_LEFT	"FlashesLeft"	/* u */
#define FWUPD_RESULT_KEY_DEVICE_VENDOR		"DeviceVendor"	/* s */
#define FWUPD_RESULT_KEY_GUID			"Guid"		/* s */
#define FWUPD_RESULT_KEY_UNIQUE_ID		"UniqueID"	/* s */
#define FWUPD_RESULT_KEY_UPDATE_DESCRIPTION	"UpdateDescription" /* s */
#define FWUPD_RESULT_KEY_UPDATE_ERROR		"PendingError"	/* s */
#define FWUPD_RESULT_KEY_UPDATE_FILENAME	"FilenameCab"	/* s */
#define FWUPD_RESULT_KEY_UPDATE_CHECKSUM	"UpdateHash"	/* s */
#define FWUPD_RESULT_KEY_UPDATE_CHECKSUM_KIND	"UpdateChecksumKind"	/* u */
#define FWUPD_RESULT_KEY_UPDATE_ID		"AppstreamId"	/* s */
#define FWUPD_RESULT_KEY_UPDATE_LICENSE		"License"	/* s */
#define FWUPD_RESULT_KEY_UPDATE_NAME		"Name"		/* s */
#define FWUPD_RESULT_KEY_UPDATE_SIZE		"Size"		/* t */
#define FWUPD_RESULT_KEY_UPDATE_STATE		"PendingState"	/* s */
#define FWUPD_RESULT_KEY_UPDATE_SUMMARY		"Summary"	/* s */
#define FWUPD_RESULT_KEY_UPDATE_TRUST_FLAGS	"Trusted"	/* t */
#define FWUPD_RESULT_KEY_UPDATE_URI		"UpdateUri"	/* s */
#define FWUPD_RESULT_KEY_UPDATE_HOMEPAGE	"UrlHomepage"	/* s */
#define FWUPD_RESULT_KEY_UPDATE_VENDOR		"Vendor"	/* s */
#define FWUPD_RESULT_KEY_UPDATE_VERSION		"UpdateVersion"	/* s */

#endif /* __FWUPD_ENUMS_PRIVATE_H */
