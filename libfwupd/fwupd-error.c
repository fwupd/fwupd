/*
 * Copyright 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <gio/gio.h>

#include "fwupd-common.h"
#include "fwupd-enums.h"
#include "fwupd-error.h"

/**
 * fwupd_error_to_string:
 * @error: an enumerated error, e.g. %FWUPD_ERROR_VERSION_NEWER
 *
 * Converts an enumerated error to a string.
 *
 * Returns: identifier string
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_error_to_string(FwupdError error)
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
	if (error == FWUPD_ERROR_AUTH_EXPIRED)
		return FWUPD_DBUS_INTERFACE ".AuthExpired";
	if (error == FWUPD_ERROR_INVALID_DATA)
		return FWUPD_DBUS_INTERFACE ".InvalidData";
	if (error == FWUPD_ERROR_TIMED_OUT)
		return FWUPD_DBUS_INTERFACE ".TimedOut";
	if (error == FWUPD_ERROR_BUSY)
		return FWUPD_DBUS_INTERFACE ".Busy";
	if (error == FWUPD_ERROR_NOT_REACHABLE)
		return FWUPD_DBUS_INTERFACE ".NotReachable";
	return NULL;
}

/**
 * fwupd_error_from_string:
 * @error: (nullable): a string, e.g. `org.freedesktop.fwupd.VersionNewer`
 *
 * Converts a string to an enumerated error.
 *
 * Returns: enumerated value
 *
 * Since: 0.7.0
 **/
FwupdError
fwupd_error_from_string(const gchar *error)
{
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".Internal") == 0)
		return FWUPD_ERROR_INTERNAL;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".VersionNewer") == 0)
		return FWUPD_ERROR_VERSION_NEWER;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".VersionSame") == 0)
		return FWUPD_ERROR_VERSION_SAME;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".AlreadyPending") == 0)
		return FWUPD_ERROR_ALREADY_PENDING;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".AuthFailed") == 0)
		return FWUPD_ERROR_AUTH_FAILED;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".Read") == 0)
		return FWUPD_ERROR_READ;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".Write") == 0)
		return FWUPD_ERROR_WRITE;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".InvalidFile") == 0)
		return FWUPD_ERROR_INVALID_FILE;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".InvalidData") == 0)
		return FWUPD_ERROR_INVALID_DATA;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".NotFound") == 0)
		return FWUPD_ERROR_NOT_FOUND;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".NothingToDo") == 0)
		return FWUPD_ERROR_NOTHING_TO_DO;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".NotSupported") == 0)
		return FWUPD_ERROR_NOT_SUPPORTED;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".SignatureInvalid") == 0)
		return FWUPD_ERROR_SIGNATURE_INVALID;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".AcPowerRequired") == 0)
		return FWUPD_ERROR_AC_POWER_REQUIRED;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".PermissionDenied") == 0)
		return FWUPD_ERROR_PERMISSION_DENIED;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".BrokenSystem") == 0)
		return FWUPD_ERROR_BROKEN_SYSTEM;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".BatteryLevelTooLow") == 0)
		return FWUPD_ERROR_BATTERY_LEVEL_TOO_LOW;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".NeedsUserAction") == 0)
		return FWUPD_ERROR_NEEDS_USER_ACTION;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".AuthExpired") == 0)
		return FWUPD_ERROR_AUTH_EXPIRED;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".TimedOut") == 0)
		return FWUPD_ERROR_TIMED_OUT;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".Busy") == 0)
		return FWUPD_ERROR_BUSY;
	if (g_strcmp0(error, FWUPD_DBUS_INTERFACE ".NotReachable") == 0)
		return FWUPD_ERROR_NOT_REACHABLE;
	return FWUPD_ERROR_LAST;
}

/**
 * fwupd_error_quark:
 *
 * An error quark.
 *
 * Returns: an error quark
 *
 * Since: 0.1.1
 **/
GQuark
fwupd_error_quark(void)
{
	static GQuark quark = 0;
	if (!quark) {
		quark = g_quark_from_static_string("FwupdError");
		for (gint i = 0; i < FWUPD_ERROR_LAST; i++) {
			g_dbus_error_register_error(quark, i, fwupd_error_to_string(i));
		}
	}
	return quark;
}

/**
 * fwupd_error_convert:
 * @perror: (nullable): A #GError, perhaps with domain #GIOError
 *
 * Convert the error to a #FwupdError, if required.
 *
 * Since: 2.0.0
 **/
void
fwupd_error_convert(GError **perror)
{
	GError *error = (perror != NULL) ? *perror : NULL;
	struct {
		GQuark domain;
		gint code;
		FwupdError fwupd_code;
	} map[] = {
	    {G_FILE_ERROR, G_FILE_ERROR_ACCES, FWUPD_ERROR_PERMISSION_DENIED},
	    {G_FILE_ERROR, G_FILE_ERROR_AGAIN, FWUPD_ERROR_BUSY},
	    {G_FILE_ERROR, G_FILE_ERROR_BADF, FWUPD_ERROR_INTERNAL},
	    {G_FILE_ERROR, G_FILE_ERROR_EXIST, FWUPD_ERROR_PERMISSION_DENIED},
	    {G_FILE_ERROR, G_FILE_ERROR_FAILED, FWUPD_ERROR_INTERNAL},
	    {G_FILE_ERROR, G_FILE_ERROR_FAULT, FWUPD_ERROR_INTERNAL},
	    {G_FILE_ERROR, G_FILE_ERROR_INTR, FWUPD_ERROR_BUSY},
	    {G_FILE_ERROR, G_FILE_ERROR_INVAL, FWUPD_ERROR_INVALID_DATA},
	    {G_FILE_ERROR, G_FILE_ERROR_IO, FWUPD_ERROR_INTERNAL},
	    {G_FILE_ERROR, G_FILE_ERROR_ISDIR, FWUPD_ERROR_INVALID_FILE},
	    {G_FILE_ERROR, G_FILE_ERROR_LOOP, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_FILE_ERROR, G_FILE_ERROR_MFILE, FWUPD_ERROR_INTERNAL},
	    {G_FILE_ERROR, G_FILE_ERROR_NAMETOOLONG, FWUPD_ERROR_INVALID_DATA},
	    {G_FILE_ERROR, G_FILE_ERROR_NFILE, FWUPD_ERROR_BROKEN_SYSTEM},
	    {G_FILE_ERROR, G_FILE_ERROR_NODEV, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_FILE_ERROR, G_FILE_ERROR_NOENT, FWUPD_ERROR_INVALID_FILE},
	    {G_FILE_ERROR, G_FILE_ERROR_NOSPC, FWUPD_ERROR_BROKEN_SYSTEM},
	    {G_FILE_ERROR, G_FILE_ERROR_NOSYS, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_FILE_ERROR, G_FILE_ERROR_NOTDIR, FWUPD_ERROR_INVALID_FILE},
	    {G_FILE_ERROR, G_FILE_ERROR_NXIO, FWUPD_ERROR_NOT_FOUND},
	    {G_FILE_ERROR, G_FILE_ERROR_PERM, FWUPD_ERROR_PERMISSION_DENIED},
	    {G_FILE_ERROR, G_FILE_ERROR_PIPE, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_FILE_ERROR, G_FILE_ERROR_ROFS, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_FILE_ERROR, G_FILE_ERROR_TXTBSY, FWUPD_ERROR_BUSY},
	    {G_IO_ERROR, G_IO_ERROR_BUSY, FWUPD_ERROR_TIMED_OUT},
	    {G_IO_ERROR, G_IO_ERROR_CANCELLED, FWUPD_ERROR_INTERNAL},
	    {G_IO_ERROR, G_IO_ERROR_FAILED, FWUPD_ERROR_INTERNAL},
	    {G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, FWUPD_ERROR_INVALID_DATA},
	    {G_IO_ERROR, G_IO_ERROR_INVALID_DATA, FWUPD_ERROR_INVALID_DATA},
	    {G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, FWUPD_ERROR_NOT_FOUND},
	    {G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_IO_ERROR, G_IO_ERROR_NOT_FOUND, FWUPD_ERROR_NOT_FOUND},
	    {G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED, FWUPD_ERROR_INTERNAL},
	    {G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE, FWUPD_ERROR_INVALID_DATA},
	    {G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT, FWUPD_ERROR_READ},
	    {G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, FWUPD_ERROR_PERMISSION_DENIED},
	    {G_IO_ERROR, G_IO_ERROR_TIMED_OUT, FWUPD_ERROR_TIMED_OUT},
	    {G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK, FWUPD_ERROR_TIMED_OUT},
	};

	/* sanity check */
	if (error == NULL)
		return;
	if (error->domain == FWUPD_ERROR)
		return;

	/* correct some company names */
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		if (g_error_matches(error, map[i].domain, map[i].code)) {
			error->domain = FWUPD_ERROR;
			error->code = map[i].fwupd_code;
			return;
		}
	}

	/* fallback */
#ifndef SUPPORTED_BUILD
	g_critical("GError %s:%i sending over D-Bus was not converted to FwupdError",
		   g_quark_to_string(error->domain),
		   error->code);
#endif
	error->domain = FWUPD_ERROR;
	error->code = FWUPD_ERROR_INTERNAL;
}
