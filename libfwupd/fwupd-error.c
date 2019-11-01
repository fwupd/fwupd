/*
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gio.h>

#include "fwupd-common.h"
#include "fwupd-enums.h"
#include "fwupd-error.h"

/**
 * SECTION:fwupd-error
 * @short_description: an error domain shared by the daemon and library
 *
 * This file also provides helper functions to map errors to strings and back
 * again.
 *
 * See also: #fwupd-enums
 */

/**
 * fwupd_error_to_string:
 * @error: A #FwupdError, e.g. %FWUPD_ERROR_VERSION_NEWER
 *
 * Converts a #FwupdError to a string.
 *
 * Return value: identifier string
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_error_to_string (FwupdError error)
{
	if (error == FWUPD_ERROR_INTERNAL)
		return FWUPD_DBUS_INTERFACE ".Internal";
	if (error == FWUPD_ERROR_VERSION_NEWER)
		return FWUPD_DBUS_INTERFACE ".VersionNewer";
	if (error == FWUPD_ERROR_VERSION_SAME)
		return FWUPD_DBUS_INTERFACE ".VersionSame";
	if (error == FWUPD_ERROR_ALREADY_PENDING)
		return FWUPD_DBUS_INTERFACE ".AlreadyPending";
	if (error == FWUPD_ERROR_AUTH_FAILED)
		return FWUPD_DBUS_INTERFACE ".AuthFailed";
	if (error == FWUPD_ERROR_READ)
		return FWUPD_DBUS_INTERFACE ".Read";
	if (error == FWUPD_ERROR_WRITE)
		return FWUPD_DBUS_INTERFACE ".Write";
	if (error == FWUPD_ERROR_INVALID_FILE)
		return FWUPD_DBUS_INTERFACE ".InvalidFile";
	if (error == FWUPD_ERROR_NOT_FOUND)
		return FWUPD_DBUS_INTERFACE ".NotFound";
	if (error == FWUPD_ERROR_NOTHING_TO_DO)
		return FWUPD_DBUS_INTERFACE ".NothingToDo";
	if (error == FWUPD_ERROR_NOT_SUPPORTED)
		return FWUPD_DBUS_INTERFACE ".NotSupported";
	if (error == FWUPD_ERROR_SIGNATURE_INVALID)
		return FWUPD_DBUS_INTERFACE ".SignatureInvalid";
	if (error == FWUPD_ERROR_AC_POWER_REQUIRED)
		return FWUPD_DBUS_INTERFACE ".AcPowerRequired";
	if (error == FWUPD_ERROR_PERMISSION_DENIED)
		return FWUPD_DBUS_INTERFACE ".PermissionDenied";
	if (error == FWUPD_ERROR_BROKEN_SYSTEM)
		return FWUPD_DBUS_INTERFACE ".BrokenSystem";
	if (error == FWUPD_ERROR_BATTERY_LEVEL_TOO_LOW)
		return FWUPD_DBUS_INTERFACE ".BatteryLevelTooLow";
	if (error == FWUPD_ERROR_NEEDS_USER_ACTION)
		return FWUPD_DBUS_INTERFACE ".NeedsUserAction";
	return NULL;
}

/**
 * fwupd_error_from_string:
 * @error: A string, e.g. `org.freedesktop.fwupd.VersionNewer`
 *
 * Converts a string to a #FwupdError.
 *
 * Return value: enumerated value
 *
 * Since: 0.7.0
 **/
FwupdError
fwupd_error_from_string (const gchar *error)
{
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".Internal") == 0)
		return FWUPD_ERROR_INTERNAL;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".VersionNewer") == 0)
		return FWUPD_ERROR_VERSION_NEWER;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".VersionSame") == 0)
		return FWUPD_ERROR_VERSION_SAME;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".AlreadyPending") == 0)
		return FWUPD_ERROR_ALREADY_PENDING;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".AuthFailed") == 0)
		return FWUPD_ERROR_AUTH_FAILED;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".Read") == 0)
		return FWUPD_ERROR_READ;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".Write") == 0)
		return FWUPD_ERROR_WRITE;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".InvalidFile") == 0)
		return FWUPD_ERROR_INVALID_FILE;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".NotFound") == 0)
		return FWUPD_ERROR_NOT_FOUND;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".NothingToDo") == 0)
		return FWUPD_ERROR_NOTHING_TO_DO;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".NotSupported") == 0)
		return FWUPD_ERROR_NOT_SUPPORTED;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".SignatureInvalid") == 0)
		return FWUPD_ERROR_SIGNATURE_INVALID;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".AcPowerRequired") == 0)
		return FWUPD_ERROR_AC_POWER_REQUIRED;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".PermissionDenied") == 0)
		return FWUPD_ERROR_PERMISSION_DENIED;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".BrokenSystem") == 0)
		return FWUPD_ERROR_BROKEN_SYSTEM;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".BatteryLevelTooLow") == 0)
		return FWUPD_ERROR_BATTERY_LEVEL_TOO_LOW;
	if (g_strcmp0 (error, FWUPD_DBUS_INTERFACE ".NeedsUserAction") == 0)
		return FWUPD_ERROR_NEEDS_USER_ACTION;
	return FWUPD_ERROR_LAST;
}

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
		for (gint i = 0; i < FWUPD_ERROR_LAST; i++) {
			g_dbus_error_register_error (quark, i,
						     fwupd_error_to_string (i));
		}
	}
	return quark;
}
