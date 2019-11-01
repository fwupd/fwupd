/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

guint32		 fu_nitrokey_perform_crc32	(const guint8	*data,
						 gsize		 size);

#define NITROKEY_TRANSACTION_TIMEOUT		100 /* ms */
#define NITROKEY_NR_RETRIES			5

#define NITROKEY_REQUEST_DATA_LENGTH		59
#define NITROKEY_REPLY_DATA_LENGTH		53

#define NITROKEY_CMD_GET_DEVICE_STATUS		(0x20 + 14)

typedef struct __attribute__((packed)) {
	guint8		command;
	guint8		payload[NITROKEY_REQUEST_DATA_LENGTH];
	guint32		crc;
} NitrokeyHidRequest;

typedef struct __attribute__((packed)) {
	guint8		device_status;
	guint8		command_id;
	guint32		last_command_crc;
	guint8		last_command_status;
	guint8		payload[NITROKEY_REPLY_DATA_LENGTH];
	guint32		crc;
} NitrokeyHidResponse;

/* based from libnitrokey/stick20_commands.h from libnitrokey v3.4.1 */
typedef struct __attribute__((packed)) {
	guint8		_padding[18]; /* stick20_commands.h:132 // 26 - 8 = 18 */
	guint8		SendCounter;
	guint8		SendDataType;
	guint8		FollowBytesFlag;
	guint8		SendSize;
	guint16		MagicNumber_StickConfig;
	guint8		ReadWriteFlagUncryptedVolume;
	guint8		ReadWriteFlagCryptedVolume;
	guint8		VersionMajor;
	guint8		VersionMinor;
	guint8		VersionReservedByte;
	guint8		VersionBuildIteration;
	guint8		ReadWriteFlagHiddenVolume;
	guint8		FirmwareLocked;
	guint8		NewSDCardFound;
	guint8		SDFillWithRandomChars;
	guint32		ActiveSD_CardID;
	guint8		VolumeActiceFlag;
	guint8		NewSmartCardFound;
	guint8		UserPwRetryCount;
	guint8		AdminPwRetryCount;
	guint32		ActiveSmartCardID;
	guint8		StickKeysNotInitiated;
} NitrokeyGetDeviceStatusPayload;
