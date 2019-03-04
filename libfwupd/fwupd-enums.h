/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

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
 * @FWUPD_STATUS_SHUTDOWN:			The daemon is shutting down
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
	FWUPD_STATUS_SHUTDOWN,				/* Since: 1.2.1 */
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
 * @FWUPD_DEVICE_FLAG_UPDATABLE:		Device is updatable in this or any other mode
 * @FWUPD_DEVICE_FLAG_ONLY_OFFLINE:		Update can only be done from offline mode
 * @FWUPD_DEVICE_FLAG_REQUIRE_AC:		Requires AC power
 * @FWUPD_DEVICE_FLAG_LOCKED:			Is locked and can be unlocked
 * @FWUPD_DEVICE_FLAG_SUPPORTED:		Is found in current metadata
 * @FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER:		Requires a bootloader mode to be manually enabled by the user
 * @FWUPD_DEVICE_FLAG_REGISTERED:		Has been registered with other plugins
 * @FWUPD_DEVICE_FLAG_NEEDS_REBOOT:		Requires a reboot to apply firmware or to reload hardware
 * @FWUPD_DEVICE_FLAG_REPORTED:			Has been reported to a metadata server
 * @FWUPD_DEVICE_FLAG_NOTIFIED:			User has been notified
 * @FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION:	Always use the runtime version rather than the bootloader
 * @FWUPD_DEVICE_FLAG_INSTALL_PARENT_FIRST:	Install composite firmware on the parent before the child
 * @FWUPD_DEVICE_FLAG_IS_BOOTLOADER:		Is currently in bootloader mode
 * @FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG:		The hardware is waiting to be replugged
 * @FWUPD_DEVICE_FLAG_IGNORE_VALIDATION:	Ignore validation safety checks when flashing this device
 * @FWUPD_DEVICE_FLAG_TRUSTED:			Extra metadata can be exposed about this device
 * @FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN:		Requires system shutdown to apply firmware
 * @FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED:	Requires the update to be retried with a new plugin
 * @FWUPD_DEVICE_FLAG_NO_AUTO_INSTANCE_IDS:	Do not add instance IDs from the device baseclass
 * @FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION:		Device update needs to be separately activated
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
#define FWUPD_DEVICE_FLAG_REPORTED		(1u << 9)	/* Since: 1.0.4 */
#define FWUPD_DEVICE_FLAG_NOTIFIED		(1u << 10)	/* Since: 1.0.5 */
#define FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION	(1u << 11)	/* Since: 1.0.6 */
#define FWUPD_DEVICE_FLAG_INSTALL_PARENT_FIRST	(1u << 12)	/* Since: 1.0.8 */
#define FWUPD_DEVICE_FLAG_IS_BOOTLOADER		(1u << 13)	/* Since: 1.0.8 */
#define FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG	(1u << 14)	/* Since: 1.1.2 */
#define FWUPD_DEVICE_FLAG_IGNORE_VALIDATION	(1u << 15)	/* Since: 1.1.2 */
#define FWUPD_DEVICE_FLAG_TRUSTED		(1u << 16)	/* Since: 1.1.2 */
#define FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN	(1u << 17)	/* Since: 1.2.4 */
#define FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED (1u << 18)	/* Since: 1.2.5 */
#define FWUPD_DEVICE_FLAG_NO_AUTO_INSTANCE_IDS	(1u << 19)	/* Since: 1.2.5 */
#define FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION	(1u << 20)	/* Since: 1.2.6 */
#define FWUPD_DEVICE_FLAG_UNKNOWN		G_MAXUINT64	/* Since: 0.7.3 */
typedef guint64 FwupdDeviceFlags;

/**
 * FwupdReleaseFlags:
 * @FWUPD_RELEASE_FLAG_NONE:			No flags set
 * @FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD:		The payload binary is trusted
 * @FWUPD_RELEASE_FLAG_TRUSTED_METADATA:	The payload metadata is trusted
 * @FWUPD_RELEASE_FLAG_IS_UPGRADE:		Is newer than the device version
 * @FWUPD_RELEASE_FLAG_IS_DOWNGRADE:		Is older than the device version
 * @FWUPD_RELEASE_FLAG_BLOCKED_VERSION:		Blocked as below device version-lowest
 * @FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL:	Blocked as release not approved
 *
 * The release flags.
 **/
#define FWUPD_RELEASE_FLAG_NONE			(0u)		/* Since: 1.2.6 */
#define FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD	(1u << 0)	/* Since: 1.2.6 */
#define FWUPD_RELEASE_FLAG_TRUSTED_METADATA	(1u << 1)	/* Since: 1.2.6 */
#define FWUPD_RELEASE_FLAG_IS_UPGRADE		(1u << 2)	/* Since: 1.2.6 */
#define FWUPD_RELEASE_FLAG_IS_DOWNGRADE		(1u << 3)	/* Since: 1.2.6 */
#define FWUPD_RELEASE_FLAG_BLOCKED_VERSION	(1u << 4)	/* Since: 1.2.6 */
#define FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL	(1u << 5)	/* Since: 1.2.6 */
#define FWUPD_RELEASE_FLAG_UNKNOWN		G_MAXUINT64	/* Since: 1.2.6 */
typedef guint64 FwupdReleaseFlags;

/**
 * FwupdInstallFlags:
 * @FWUPD_INSTALL_FLAG_NONE:			No flags set
 * @FWUPD_INSTALL_FLAG_OFFLINE:			Schedule this for next boot
 * @FWUPD_INSTALL_FLAG_ALLOW_REINSTALL:		Allow reinstalling the same version
 * @FWUPD_INSTALL_FLAG_ALLOW_OLDER:		Allow downgrading firmware
 * @FWUPD_INSTALL_FLAG_FORCE:			Force the update even if not a good idea
 * @FWUPD_INSTALL_FLAG_NO_HISTORY:		Do not write to the history database
 *
 * Flags to set when performing the firwmare update or install.
 **/
typedef enum {
	FWUPD_INSTALL_FLAG_NONE			= 0,		/* Since: 0.7.0 */
	FWUPD_INSTALL_FLAG_OFFLINE		= 1 << 0,	/* Since: 0.7.0 */
	FWUPD_INSTALL_FLAG_ALLOW_REINSTALL	= 1 << 1,	/* Since: 0.7.0 */
	FWUPD_INSTALL_FLAG_ALLOW_OLDER		= 1 << 2,	/* Since: 0.7.0 */
	FWUPD_INSTALL_FLAG_FORCE		= 1 << 3,	/* Since: 0.7.1 */
	FWUPD_INSTALL_FLAG_NO_HISTORY		= 1 << 4,	/* Since: 1.0.8 */
	/*< private >*/
	FWUPD_INSTALL_FLAG_LAST
} FwupdInstallFlags;

/**
 * FwupdUpdateState:
 * @FWUPD_UPDATE_STATE_UNKNOWN:			Unknown
 * @FWUPD_UPDATE_STATE_PENDING:			Update is pending
 * @FWUPD_UPDATE_STATE_SUCCESS:			Update was successfull
 * @FWUPD_UPDATE_STATE_FAILED:			Update failed
 * @FWUPD_UPDATE_STATE_NEEDS_REBOOT:		Waiting for a reboot to apply
 *
 * The update state.
 **/
typedef enum {
	FWUPD_UPDATE_STATE_UNKNOWN,			/* Since: 0.7.0 */
	FWUPD_UPDATE_STATE_PENDING,			/* Since: 0.7.0 */
	FWUPD_UPDATE_STATE_SUCCESS,			/* Since: 0.7.0 */
	FWUPD_UPDATE_STATE_FAILED,			/* Since: 0.7.0 */
	FWUPD_UPDATE_STATE_NEEDS_REBOOT,		/* Since: 1.0.4 */
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
const gchar	*fwupd_release_flag_to_string		(FwupdReleaseFlags release_flag);
FwupdReleaseFlags fwupd_release_flag_from_string	(const gchar	*release_flag);
const gchar	*fwupd_update_state_to_string		(FwupdUpdateState update_state);
FwupdUpdateState fwupd_update_state_from_string		(const gchar	*update_state);
const gchar	*fwupd_trust_flag_to_string		(FwupdTrustFlags trust_flag);
FwupdTrustFlags	 fwupd_trust_flag_from_string		(const gchar	*trust_flag);
FwupdKeyringKind fwupd_keyring_kind_from_string		(const gchar	*keyring_kind);
const gchar	*fwupd_keyring_kind_to_string		(FwupdKeyringKind keyring_kind);

G_END_DECLS
