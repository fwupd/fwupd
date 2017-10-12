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

/**
 * FwupdStatus:
 * @FWUPD_STATUS_UNKNOWN:			Unknown state
 * @FWUPD_STATUS_IDLE:				Idle
 * @FWUPD_STATUS_LOADING:			Loading a resource
 * @FWUPD_STATUS_DECOMPRESSING:			Decompressing firmware
 * @FWUPD_STATUS_DEVICE_RESTART:		Restarting the device
 * @FWUPD_STATUS_DEVICE_WRITE:			Writing to a device
 * @FWUPD_STATUS_DEVICE_VERIFY:			Verifying (reading) a device
 * @FWUPD_STATUS_SCHEDULING:			Scheduling an offline update
 * @FWUPD_STATUS_DOWNLOADING:			A file is downloading
 * @FWUPD_STATUS_DEVICE_READ:			Reading from a device
 * @FWUPD_STATUS_DEVICE_ERASE:			Erasing a device
 * @FWUPD_STATUS_WAITING_FOR_AUTH:		Waiting for authentication
 * @FWUPD_STATUS_DEVICE_BUSY:			The device is busy
 *
 * The flags to show daemon status.
 **/
typedef enum {
	FWUPD_STATUS_UNKNOWN,				/* Since: 0.1.1 */
	FWUPD_STATUS_IDLE,				/* Since: 0.1.1 */
	FWUPD_STATUS_LOADING,				/* Since: 0.1.1 */
	FWUPD_STATUS_DECOMPRESSING,			/* Since: 0.1.1 */
	FWUPD_STATUS_DEVICE_RESTART,			/* Since: 0.1.1 */
	FWUPD_STATUS_DEVICE_WRITE,			/* Since: 0.1.1 */
	FWUPD_STATUS_DEVICE_VERIFY,			/* Since: 0.1.1 */
	FWUPD_STATUS_SCHEDULING,			/* Since: 0.1.1 */
	FWUPD_STATUS_DOWNLOADING,			/* Since: 0.9.4 */
	FWUPD_STATUS_DEVICE_READ,			/* Since: 1.0.0 */
	FWUPD_STATUS_DEVICE_ERASE,			/* Since: 1.0.0 */
	FWUPD_STATUS_WAITING_FOR_AUTH,			/* Since: 1.0.0 */
	FWUPD_STATUS_DEVICE_BUSY,			/* Since: 1.0.1 */
	/*< private >*/
	FWUPD_STATUS_LAST
} FwupdStatus;

/**
 * FwupdTrustFlags:
 * @FWUPD_TRUST_FLAG_NONE:			No trust
 * @FWUPD_TRUST_FLAG_PAYLOAD:			The firmware is trusted
 * @FWUPD_TRUST_FLAG_METADATA:			The metadata is trusted
 *
 * The flags to show the level of trust.
 **/
typedef enum {
	FWUPD_TRUST_FLAG_NONE		= 0,		/* Since: 0.1.2 */
	FWUPD_TRUST_FLAG_PAYLOAD	= 1 << 0,	/* Since: 0.1.2 */
	FWUPD_TRUST_FLAG_METADATA	= 1 << 1,	/* Since: 0.1.2 */
	/*< private >*/
	FWUPD_TRUST_FLAG_LAST
} FwupdTrustFlags;

/**
 * FwupdDeviceFlags:
 * @FWUPD_DEVICE_FLAG_NONE:			No flags set
 * @FWUPD_DEVICE_FLAG_INTERNAL:			Device cannot be removed easily
 * @FWUPD_DEVICE_FLAG_UPDATABLE:		Device is updatable
 * @FWUPD_DEVICE_FLAG_ONLY_OFFLINE:		Update can only be done from offline mode
 * @FWUPD_DEVICE_FLAG_REQUIRE_AC:		Requires AC power
 * @FWUPD_DEVICE_FLAG_LOCKED:			Is locked and can be unlocked
 * @FWUPD_DEVICE_FLAG_SUPPORTED:		Is found in current metadata
 * @FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER:		Requires a bootloader mode
 * @FWUPD_DEVICE_FLAG_REGISTERED:		Has been registered with other plugins
 * @FWUPD_DEVICE_FLAG_NEEDS_REBOOT:		Requires a reboot to apply firmware or to reload hardware
 *
 * The device flags.
 **/
#define FWUPD_DEVICE_FLAG_NONE			(0u)		/* Since: 0.1.3 */
#define FWUPD_DEVICE_FLAG_INTERNAL		(1u << 0)	/* Since: 0.1.3 */
#define FWUPD_DEVICE_FLAG_UPDATABLE		(1u << 1)	/* Since: 0.9.7 */
#define FWUPD_DEVICE_FLAG_ONLY_OFFLINE		(1u << 2)	/* Since: 0.9.7 */
#define FWUPD_DEVICE_FLAG_REQUIRE_AC		(1u << 3)	/* Since: 0.6.3 */
#define FWUPD_DEVICE_FLAG_LOCKED		(1u << 4)	/* Since: 0.6.3 */
#define FWUPD_DEVICE_FLAG_SUPPORTED		(1u << 5)	/* Since: 0.7.1 */
#define FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER	(1u << 6)	/* Since: 0.7.3 */
#define FWUPD_DEVICE_FLAG_REGISTERED		(1u << 7)	/* Since: 0.9.7 */
#define FWUPD_DEVICE_FLAG_NEEDS_REBOOT		(1u << 8)	/* Since: 0.9.7 */
#define FWUPD_DEVICE_FLAG_UNKNOWN		G_MAXUINT64	/* Since: 0.7.3 */
typedef guint64 FwupdDeviceFlags;

/**
 * FwupdInstallFlags:
 * @FWUPD_INSTALL_FLAG_NONE:			No flags set
 * @FWUPD_INSTALL_FLAG_OFFLINE:			Schedule this for next boot
 * @FWUPD_INSTALL_FLAG_ALLOW_REINSTALL:		Allow reinstalling the same version
 * @FWUPD_INSTALL_FLAG_ALLOW_OLDER:		Allow downgrading firmware
 * @FWUPD_INSTALL_FLAG_FORCE:			Force the update even if not a good idea
 *
 * Flags to set when performing the firwmare update or install.
 **/
typedef enum {
	FWUPD_INSTALL_FLAG_NONE			= 0,	/* Since: 0.7.0 */
	FWUPD_INSTALL_FLAG_OFFLINE		= 1,	/* Since: 0.7.0 */
	FWUPD_INSTALL_FLAG_ALLOW_REINSTALL	= 2,	/* Since: 0.7.0 */
	FWUPD_INSTALL_FLAG_ALLOW_OLDER		= 4,	/* Since: 0.7.0 */
	FWUPD_INSTALL_FLAG_FORCE		= 8,	/* Since: 0.7.1 */
	/*< private >*/
	FWUPD_INSTALL_FLAG_LAST
} FwupdInstallFlags;

/**
 * FwupdUpdateState:
 * @FWUPD_UPDATE_STATE_UNKNOWN:			Unknown
 * @FWUPD_UPDATE_STATE_PENDING:			Update is pending
 * @FWUPD_UPDATE_STATE_SUCCESS:			Update was successfull
 * @FWUPD_UPDATE_STATE_FAILED:			Update failed
 *
 * The update state.
 **/
typedef enum {
	FWUPD_UPDATE_STATE_UNKNOWN,			/* Since: 0.7.0 */
	FWUPD_UPDATE_STATE_PENDING,			/* Since: 0.7.0 */
	FWUPD_UPDATE_STATE_SUCCESS,			/* Since: 0.7.0 */
	FWUPD_UPDATE_STATE_FAILED,			/* Since: 0.7.0 */
	/*< private >*/
	FWUPD_UPDATE_STATE_LAST
} FwupdUpdateState;

/**
 * FwupdKeyringKind:
 * @FWUPD_KEYRING_KIND_UNKNOWN:			Unknown
 * @FWUPD_KEYRING_KIND_NONE:			No verification
 * @FWUPD_KEYRING_KIND_GPG:			Verification using GPG
 * @FWUPD_KEYRING_KIND_PKCS7:			Verification using PKCS7
 *
 * The update state.
 **/
typedef enum {
	FWUPD_KEYRING_KIND_UNKNOWN,			/* Since: 0.9.7 */
	FWUPD_KEYRING_KIND_NONE,			/* Since: 0.9.7 */
	FWUPD_KEYRING_KIND_GPG,				/* Since: 0.9.7 */
	FWUPD_KEYRING_KIND_PKCS7,			/* Since: 0.9.7 */
	/*< private >*/
	FWUPD_KEYRING_KIND_LAST
} FwupdKeyringKind;

const gchar	*fwupd_status_to_string			(FwupdStatus	 status);
FwupdStatus	 fwupd_status_from_string		(const gchar	*status);
const gchar	*fwupd_device_flag_to_string		(FwupdDeviceFlags device_flag);
FwupdDeviceFlags fwupd_device_flag_from_string		(const gchar	*device_flag);
const gchar	*fwupd_update_state_to_string		(FwupdUpdateState update_state);
FwupdUpdateState fwupd_update_state_from_string		(const gchar	*update_state);
const gchar	*fwupd_trust_flag_to_string		(FwupdTrustFlags trust_flag);
FwupdTrustFlags	 fwupd_trust_flag_from_string		(const gchar	*trust_flag);
FwupdKeyringKind fwupd_keyring_kind_from_string		(const gchar	*keyring_kind);
const gchar	*fwupd_keyring_kind_to_string		(FwupdKeyringKind keyring_kind);

#endif /* __FWUPD_ENUMS_H */
