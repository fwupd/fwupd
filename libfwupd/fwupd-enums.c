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

#include "config.h"

#include "fwupd-enums.h"

/**
 * fwupd_status_to_string:
 * @status: A #FwupdStatus, e.g. %FWUPD_STATUS_DECOMPRESSING
 *
 * Converts a #FwupdStatus to a string.
 *
 * Return value: identifier string
 *
 * Since: 0.1.1
 **/
const gchar *
fwupd_status_to_string (FwupdStatus status)
{
	if (status == FWUPD_STATUS_UNKNOWN)
		return "unknown";
	if (status == FWUPD_STATUS_IDLE)
		return "idle";
	if (status == FWUPD_STATUS_DECOMPRESSING)
		return "decompressing";
	if (status == FWUPD_STATUS_LOADING)
		return "loading";
	if (status == FWUPD_STATUS_DEVICE_RESTART)
		return "device-restart";
	if (status == FWUPD_STATUS_DEVICE_WRITE)
		return "device-write";
	if (status == FWUPD_STATUS_DEVICE_VERIFY)
		return "device-verify";
	if (status == FWUPD_STATUS_SCHEDULING)
		return "scheduling";
	return NULL;
}

/**
 * fwupd_status_from_string:
 * @status: A string, e.g. "decompressing"
 *
 * Converts a string to a #FwupdStatus.
 *
 * Return value: enumerated value
 *
 * Since: 0.1.1
 **/
FwupdStatus
fwupd_status_from_string (const gchar *status)
{
	if (g_strcmp0 (status, "unknown") == 0)
		return FWUPD_STATUS_UNKNOWN;
	if (g_strcmp0 (status, "idle") == 0)
		return FWUPD_STATUS_IDLE;
	if (g_strcmp0 (status, "decompressing") == 0)
		return FWUPD_STATUS_DECOMPRESSING;
	if (g_strcmp0 (status, "loading") == 0)
		return FWUPD_STATUS_LOADING;
	if (g_strcmp0 (status, "device-restart") == 0)
		return FWUPD_STATUS_DEVICE_RESTART;
	if (g_strcmp0 (status, "device-write") == 0)
		return FWUPD_STATUS_DEVICE_WRITE;
	if (g_strcmp0 (status, "device-verify") == 0)
		return FWUPD_STATUS_DEVICE_VERIFY;
	if (g_strcmp0 (status, "scheduling") == 0)
		return FWUPD_STATUS_SCHEDULING;
	return FWUPD_STATUS_LAST;
}

/**
 * fwupd_device_flag_to_string:
 * @device_flag: A #FwupdDeviceFlags, e.g. %FWUPD_DEVICE_FLAG_REQUIRE_AC
 *
 * Converts a #FwupdDeviceFlags to a string.
 *
 * Return value: identifier string
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_device_flag_to_string (FwupdDeviceFlags device_flag)
{
	if (device_flag == FWUPD_DEVICE_FLAG_NONE)
		return "none";
	if (device_flag == FWUPD_DEVICE_FLAG_INTERNAL)
		return "internal";
	if (device_flag == FWUPD_DEVICE_FLAG_ALLOW_ONLINE)
		return "allow-online";
	if (device_flag == FWUPD_DEVICE_FLAG_ALLOW_OFFLINE)
		return "allow-offline";
	if (device_flag == FWUPD_DEVICE_FLAG_REQUIRE_AC)
		return "require-ac";
	if (device_flag == FWUPD_DEVICE_FLAG_LOCKED)
		return "locked";
	if (device_flag == FWUPD_DEVICE_FLAG_SUPPORTED)
		return "supported";
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER)
		return "needs-bootloader";
	if (device_flag == FWUPD_DEVICE_FLAG_UNKNOWN)
		return "unknown";
	return NULL;
}

/**
 * fwupd_device_flag_from_string:
 * @device_flag: A string, e.g. "require-ac"
 *
 * Converts a string to a #FwupdDeviceFlags.
 *
 * Return value: enumerated value
 *
 * Since: 0.7.0
 **/
FwupdDeviceFlags
fwupd_device_flag_from_string (const gchar *device_flag)
{
	if (g_strcmp0 (device_flag, "none") == 0)
		return FWUPD_DEVICE_FLAG_NONE;
	if (g_strcmp0 (device_flag, "internal") == 0)
		return FWUPD_DEVICE_FLAG_INTERNAL;
	if (g_strcmp0 (device_flag, "allow-online") == 0)
		return FWUPD_DEVICE_FLAG_ALLOW_ONLINE;
	if (g_strcmp0 (device_flag, "allow-offline") == 0)
		return FWUPD_DEVICE_FLAG_ALLOW_OFFLINE;
	if (g_strcmp0 (device_flag, "require-ac") == 0)
		return FWUPD_DEVICE_FLAG_REQUIRE_AC;
	if (g_strcmp0 (device_flag, "locked") == 0)
		return FWUPD_DEVICE_FLAG_LOCKED;
	if (g_strcmp0 (device_flag, "supported") == 0)
		return FWUPD_DEVICE_FLAG_SUPPORTED;
	if (g_strcmp0 (device_flag, "needs-bootloader") == 0)
		return FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER;
	return FWUPD_DEVICE_FLAG_UNKNOWN;
}

/**
 * fwupd_update_state_to_string:
 * @update_state: A #FwupdUpdateState, e.g. %FWUPD_UPDATE_STATE_PENDING
 *
 * Converts a #FwupdUpdateState to a string.
 *
 * Return value: identifier string
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_update_state_to_string (FwupdUpdateState update_state)
{
	if (update_state == FWUPD_UPDATE_STATE_UNKNOWN)
		return "unknown";
	if (update_state == FWUPD_UPDATE_STATE_PENDING)
		return "pending";
	if (update_state == FWUPD_UPDATE_STATE_SUCCESS)
		return "success";
	if (update_state == FWUPD_UPDATE_STATE_FAILED)
		return "failed";
	return NULL;
}

/**
 * fwupd_update_state_from_string:
 * @update_state: A string, e.g. "pending"
 *
 * Converts a string to a #FwupdUpdateState.
 *
 * Return value: enumerated value
 *
 * Since: 0.7.0
 **/
FwupdUpdateState
fwupd_update_state_from_string (const gchar *update_state)
{
	if (g_strcmp0 (update_state, "unknown") == 0)
		return FWUPD_UPDATE_STATE_UNKNOWN;
	if (g_strcmp0 (update_state, "pending") == 0)
		return FWUPD_UPDATE_STATE_PENDING;
	if (g_strcmp0 (update_state, "success") == 0)
		return FWUPD_UPDATE_STATE_SUCCESS;
	if (g_strcmp0 (update_state, "failed") == 0)
		return FWUPD_UPDATE_STATE_FAILED;
	return FWUPD_UPDATE_STATE_UNKNOWN;
}

/**
 * fwupd_trust_flag_to_string:
 * @trust_flag: A #FwupdTrustFlags, e.g. %FWUPD_TRUST_FLAG_PAYLOAD
 *
 * Converts a #FwupdTrustFlags to a string.
 *
 * Return value: identifier string
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_trust_flag_to_string (FwupdTrustFlags trust_flag)
{
	if (trust_flag == FWUPD_TRUST_FLAG_NONE)
		return "none";
	if (trust_flag == FWUPD_TRUST_FLAG_PAYLOAD)
		return "payload";
	if (trust_flag == FWUPD_TRUST_FLAG_METADATA)
		return "metadata";
	return NULL;
}

/**
 * fwupd_trust_flag_from_string:
 * @trust_flag: A string, e.g. "payload"
 *
 * Converts a string to a #FwupdTrustFlags.
 *
 * Return value: enumerated value
 *
 * Since: 0.7.0
 **/
FwupdTrustFlags
fwupd_trust_flag_from_string (const gchar *trust_flag)
{
	if (g_strcmp0 (trust_flag, "none") == 0)
		return FWUPD_TRUST_FLAG_NONE;
	if (g_strcmp0 (trust_flag, "payload") == 0)
		return FWUPD_TRUST_FLAG_PAYLOAD;
	if (g_strcmp0 (trust_flag, "metadata") == 0)
		return FWUPD_TRUST_FLAG_METADATA;
	return FWUPD_TRUST_FLAG_LAST;
}
