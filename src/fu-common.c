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

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>

#include "fu-common.h"

/**
 * fu_status_to_string:
 **/
const gchar *
fu_status_to_string (FuStatus status)
{
	if (status == FU_STATUS_IDLE)
		return "idle";
	if (status == FU_STATUS_DECOMPRESSING)
		return "decompressing";
	if (status == FU_STATUS_LOADING)
		return "loading";
	if (status == FU_STATUS_DEVICE_RESTART)
		return "device-restart";
	if (status == FU_STATUS_DEVICE_WRITE)
		return "device-write";
	if (status == FU_STATUS_DEVICE_VERIFY)
		return "device-verify";
	if (status == FU_STATUS_SCHEDULING)
		return "scheduling";
	return NULL;
}

/**
 * fu_status_from_string:
 **/
FuStatus
fu_status_from_string (const gchar *status)
{
	if (g_strcmp0 (status, "idle") == 0)
		return FU_STATUS_IDLE;
	if (g_strcmp0 (status, "decompressing") == 0)
		return FU_STATUS_DECOMPRESSING;
	if (g_strcmp0 (status, "loading") == 0)
		return FU_STATUS_LOADING;
	if (g_strcmp0 (status, "device-restart") == 0)
		return FU_STATUS_DEVICE_RESTART;
	if (g_strcmp0 (status, "device-write") == 0)
		return FU_STATUS_DEVICE_WRITE;
	if (g_strcmp0 (status, "device-verify") == 0)
		return FU_STATUS_DEVICE_VERIFY;
	if (g_strcmp0 (status, "scheduling") == 0)
		return FU_STATUS_SCHEDULING;
	return FU_STATUS_IDLE;
}

/**
 * fu_error_quark:
 *
 * Return value: An error quark.
 *
 * Since: 0.1.0
 **/
GQuark
fu_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark) {
		quark = g_quark_from_static_string ("FuError");
		g_dbus_error_register_error (quark,
					     FU_ERROR_INTERNAL,
					     "org.freedesktop.fwupd.InternalError");
		g_dbus_error_register_error (quark,
					     FU_ERROR_ALREADY_NEWER_VERSION,
					     "org.freedesktop.fwupd.AlreadyNewerVersion");
		g_dbus_error_register_error (quark,
					     FU_ERROR_ALREADY_SAME_VERSION,
					     "org.freedesktop.fwupd.AlreadySameVersion");
		g_dbus_error_register_error (quark,
					     FU_ERROR_ALREADY_SCHEDULED,
					     "org.freedesktop.fwupd.AlreadyScheduled");
		g_dbus_error_register_error (quark,
					     FU_ERROR_FAILED_TO_AUTHENTICATE,
					     "org.freedesktop.fwupd.FailedToAuthenticate");
		g_dbus_error_register_error (quark,
					     FU_ERROR_FAILED_TO_READ,
					     "org.freedesktop.fwupd.FailedToRead");
		g_dbus_error_register_error (quark,
					     FU_ERROR_FAILED_TO_WRITE,
					     "org.freedesktop.fwupd.FailedToWrite");
		g_dbus_error_register_error (quark,
					     FU_ERROR_INVALID_FILE,
					     "org.freedesktop.fwupd.InvalidFile");
		g_dbus_error_register_error (quark,
					     FU_ERROR_NO_SUCH_DEVICE,
					     "org.freedesktop.fwupd.NoSuchDevice");
		g_dbus_error_register_error (quark,
					     FU_ERROR_NO_SUCH_METHOD,
					     "org.freedesktop.fwupd.NoSuchMethod");
		g_dbus_error_register_error (quark,
					     FU_ERROR_NO_SUCH_PROPERTY,
					     "org.freedesktop.fwupd.NoSuchProperty");
		g_dbus_error_register_error (quark,
					     FU_ERROR_NOTHING_TO_DO,
					     "org.freedesktop.fwupd.NothingToDo");
		g_dbus_error_register_error (quark,
					     FU_ERROR_NOT_POSSIBLE,
					     "org.freedesktop.fwupd.NotPossible");
	}
	return quark;
}
