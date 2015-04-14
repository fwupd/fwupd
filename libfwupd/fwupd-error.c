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

#include <gio/gio.h>

#include "fwupd-error.h"

/**
 * fwupd_error_quark:
 *
 * Return value: An error quark.
 *
 * Since: 0.1.1
 **/
GQuark
fwupd_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark) {
		quark = g_quark_from_static_string ("FwupdError");
		g_dbus_error_register_error (quark,
					     FWUPD_ERROR_INTERNAL,
					     "org.freedesktop.fwupd.Internal");
		g_dbus_error_register_error (quark,
					     FWUPD_ERROR_VERSION_NEWER,
					     "org.freedesktop.fwupd.VersionNewer");
		g_dbus_error_register_error (quark,
					     FWUPD_ERROR_VERSION_SAME,
					     "org.freedesktop.fwupd.VersionSame");
		g_dbus_error_register_error (quark,
					     FWUPD_ERROR_ALREADY_PENDING,
					     "org.freedesktop.fwupd.AlreadyPending");
		g_dbus_error_register_error (quark,
					     FWUPD_ERROR_AUTH_FAILED,
					     "org.freedesktop.fwupd.AuthFailed");
		g_dbus_error_register_error (quark,
					     FWUPD_ERROR_READ,
					     "org.freedesktop.fwupd.Read");
		g_dbus_error_register_error (quark,
					     FWUPD_ERROR_WRITE,
					     "org.freedesktop.fwupd.Write");
		g_dbus_error_register_error (quark,
					     FWUPD_ERROR_INVALID_FILE,
					     "org.freedesktop.fwupd.InvalidFile");
		g_dbus_error_register_error (quark,
					     FWUPD_ERROR_NOT_FOUND,
					     "org.freedesktop.fwupd.NotFound");
		g_dbus_error_register_error (quark,
					     FWUPD_ERROR_NOTHING_TO_DO,
					     "org.freedesktop.fwupd.NothingToDo");
		g_dbus_error_register_error (quark,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "org.freedesktop.fwupd.NotSupported");
		g_dbus_error_register_error (quark,
					     FWUPD_ERROR_SIGNATURE_INVALID,
					     "org.freedesktop.fwupd.SignatureInvalid");
	}
	return quark;
}
