/*
 * Copyright 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define FWUPD_ERROR fwupd_error_quark()

/**
 * FwupdError:
 *
 * The error code.
 **/
typedef enum {
	/**
	 * FWUPD_ERROR_INTERNAL:
	 *
	 * Internal error.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_ERROR_INTERNAL,
	/**
	 * FWUPD_ERROR_VERSION_NEWER:
	 *
	 * Installed newer firmware version.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_ERROR_VERSION_NEWER,
	/**
	 * FWUPD_ERROR_VERSION_SAME:
	 *
	 * Installed same firmware version.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_ERROR_VERSION_SAME,
	/**
	 * FWUPD_ERROR_ALREADY_PENDING:
	 *
	 * Already set to be installed offline.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_ERROR_ALREADY_PENDING,
	/**
	 * FWUPD_ERROR_AUTH_FAILED:
	 *
	 * Failed to get authentication.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_ERROR_AUTH_FAILED,
	/**
	 * FWUPD_ERROR_READ:
	 *
	 * Failed to read from device.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_ERROR_READ,
	/**
	 * FWUPD_ERROR_WRITE:
	 *
	 * Failed to write to the device.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_ERROR_WRITE,
	/**
	 * FWUPD_ERROR_INVALID_FILE:
	 *
	 * Invalid file format.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_ERROR_INVALID_FILE,
	/**
	 * FWUPD_ERROR_NOT_FOUND:
	 *
	 * No matching device exists.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_ERROR_NOT_FOUND,
	/**
	 * FWUPD_ERROR_NOTHING_TO_DO:
	 *
	 * Nothing to do.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_ERROR_NOTHING_TO_DO,
	/**
	 * FWUPD_ERROR_NOT_SUPPORTED:
	 *
	 * Action was not possible.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_ERROR_NOT_SUPPORTED,
	/**
	 * FWUPD_ERROR_SIGNATURE_INVALID:
	 *
	 * Signature was invalid.
	 *
	 * Since: 0.1.2
	 */
	FWUPD_ERROR_SIGNATURE_INVALID,
	/**
	 * FWUPD_ERROR_AC_POWER_REQUIRED:
	 *
	 * AC power was required.
	 *
	 * Since: 0.8.0
	 */
	FWUPD_ERROR_AC_POWER_REQUIRED,
	/**
	 * FWUPD_ERROR_PERMISSION_DENIED:
	 *
	 * Permission was denied.
	 *
	 * Since: 0.9.8
	 */
	FWUPD_ERROR_PERMISSION_DENIED,
	/**
	 * FWUPD_ERROR_BROKEN_SYSTEM:
	 *
	 * User has configured their system in a broken way.
	 *
	 * Since: 1.2.8
	 */
	FWUPD_ERROR_BROKEN_SYSTEM,
	/**
	 * FWUPD_ERROR_BATTERY_LEVEL_TOO_LOW:
	 *
	 * The system battery level is too low.
	 *
	 * Since: 1.2.10
	 */
	FWUPD_ERROR_BATTERY_LEVEL_TOO_LOW,
	/**
	 * FWUPD_ERROR_NEEDS_USER_ACTION:
	 *
	 * User needs to do an action to complete the update.
	 *
	 * Since: 1.3.3
	 */
	FWUPD_ERROR_NEEDS_USER_ACTION,
	/**
	 * FWUPD_ERROR_AUTH_EXPIRED:
	 *
	 * Failed to get auth as credentials have expired.
	 *
	 * Since: 1.7.5
	 */
	FWUPD_ERROR_AUTH_EXPIRED,
	/**
	 * FWUPD_ERROR_INVALID_DATA:
	 *
	 * Invalid data.
	 *
	 * Since: 2.0.0
	 */
	FWUPD_ERROR_INVALID_DATA,
	/**
	 * FWUPD_ERROR_TIMED_OUT:
	 *
	 * The request timed out.
	 *
	 * Since: 2.0.0
	 */
	FWUPD_ERROR_TIMED_OUT,
	/**
	 * FWUPD_ERROR_BUSY:
	 *
	 * The device is busy.
	 *
	 * Since: 2.0.0
	 */
	FWUPD_ERROR_BUSY,
	/**
	 * FWUPD_ERROR_NOT_REACHABLE:
	 *
	 * The network is not reachable.
	 *
	 * Since: 2.0.4
	 */
	FWUPD_ERROR_NOT_REACHABLE,
	/*< private >*/
	FWUPD_ERROR_LAST
} FwupdError;

GQuark
fwupd_error_quark(void);
const gchar *
fwupd_error_to_string(FwupdError error);
FwupdError
fwupd_error_from_string(const gchar *error);
void
fwupd_error_convert(GError **perror);
const gchar *
fwupd_strerror(gint errnum);

G_END_DECLS
