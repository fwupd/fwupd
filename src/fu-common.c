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
