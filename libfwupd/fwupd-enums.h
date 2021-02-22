/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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
 * FwupdFeatureFlags:
 * @FWUPD_FEATURE_FLAG_NONE:			No trust
 * @FWUPD_FEATURE_FLAG_CAN_REPORT:		Can upload a report of the update back to the server
 * @FWUPD_FEATURE_FLAG_DETACH_ACTION:		Can perform detach action, typically showing text
 * @FWUPD_FEATURE_FLAG_UPDATE_ACTION:		Can perform update action, typically showing text
 * @FWUPD_FEATURE_FLAG_SWITCH_BRANCH:		Can switch the firmware branch
 *
 * The flags to the feature capabilities of the front-end client.
 **/
typedef enum {
	FWUPD_FEATURE_FLAG_NONE			= 0,		/* Since: 1.4.5 */
	FWUPD_FEATURE_FLAG_CAN_REPORT		= 1 << 0,	/* Since: 1.4.5 */
	FWUPD_FEATURE_FLAG_DETACH_ACTION	= 1 << 1,	/* Since: 1.4.5 */
	FWUPD_FEATURE_FLAG_UPDATE_ACTION	= 1 << 2,	/* Since: 1.4.5 */
	FWUPD_FEATURE_FLAG_SWITCH_BRANCH	= 1 << 3,	/* Since: 1.5.0 */
	/*< private >*/
	FWUPD_FEATURE_FLAG_LAST
} FwupdFeatureFlags;

/**
 * FwupdDeviceFlags:
 * @FWUPD_DEVICE_FLAG_NONE:			No flags set
 * @FWUPD_DEVICE_FLAG_INTERNAL:			Device cannot be removed easily
 * @FWUPD_DEVICE_FLAG_UPDATABLE:		Device is updatable in this or any other mode
 * @FWUPD_DEVICE_FLAG_ONLY_OFFLINE:		Update can only be done from offline mode
 * @FWUPD_DEVICE_FLAG_REQUIRE_AC:		System requires external power source
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
 * @FWUPD_DEVICE_FLAG_NO_AUTO_INSTANCE_IDS:	Deprecated, no not use
 * @FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION:		Device update needs to be separately activated
 * @FWUPD_DEVICE_FLAG_HISTORICAL		Device is for historical data only
 * @FWUPD_DEVICE_FLAG_ENSURE_SEMVER:		Deprecated, no not use
 * @FWUPD_DEVICE_FLAG_ONLY_SUPPORTED:		Deprecated, no not use
 * @FWUPD_DEVICE_FLAG_WILL_DISAPPEAR:		Device will disappear after update and can't be verified
 * @FWUPD_DEVICE_FLAG_CAN_VERIFY:		Device checksums can be compared against metadata
 * @FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE:		Image can be dumped from device for verification
 * @FWUPD_DEVICE_FLAG_DUAL_IMAGE:		Device update architecture uses A/B partitions for updates
 * @FWUPD_DEVICE_FLAG_SELF_RECOVERY:		In flashing mode device will only accept intended payloads
 * @FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE:	Device remains usable while fwupd flashes or schedules the update
 * @FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED:	All firmware updates for this device require a firmware version check
 * @FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES:	Install each intermediate release rather than jumping direct to newest
 * @FWUPD_DEVICE_FLAG_MD_SET_NAME:		Deprecated, no not use
 * @FWUPD_DEVICE_FLAG_MD_SET_NAME_CATEGORY:	Deprecated, no not use
 * @FWUPD_DEVICE_FLAG_MD_SET_VERFMT:		Deprecated, no not use
 * @FWUPD_DEVICE_FLAG_ADD_COUNTERPART_GUIDS:	Add counterpart GUIDs from an alternate mode like bootloader
 * @FWUPD_DEVICE_FLAG_NO_GUID_MATCHING:		Deprecated, no not use
 * @FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN:		Device is updatable but should not be called by the client
 * @FWUPD_DEVICE_FLAG_SKIPS_RESTART:		Device relies upon activation or power cycle to load firmware
 * @FWUPD_DEVICE_FLAG_HAS_MULTIPLE_BRANCHES:	Device supports switching to a different stream of firmware
 * @FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL:	Device firmware should be saved before installing firmware
 * @FWUPD_DEVICE_FLAG_MD_SET_ICON:		Deprecated, no not use
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
#define FWUPD_DEVICE_FLAG_NO_AUTO_INSTANCE_IDS	(1u << 19)	/* Since: 1.2.5; Deprecated: 1.5.5 */
#define FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION	(1u << 20)	/* Since: 1.2.6 */
#define FWUPD_DEVICE_FLAG_ENSURE_SEMVER		(1u << 21)	/* Since: 1.2.9; Deprecated: 1.5.5 */
#define FWUPD_DEVICE_FLAG_HISTORICAL		(1u << 22)	/* Since: 1.3.2 */
#define FWUPD_DEVICE_FLAG_ONLY_SUPPORTED	(1u << 23)	/* Since: 1.3.3; Deprecated: 1.5.5 */
#define FWUPD_DEVICE_FLAG_WILL_DISAPPEAR	(1u << 24)	/* Since: 1.3.3 */
#define FWUPD_DEVICE_FLAG_CAN_VERIFY		(1u << 25)	/* Since: 1.3.3 */
#define FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE	(1u << 26)	/* Since: 1.3.3 */
#define FWUPD_DEVICE_FLAG_DUAL_IMAGE		(1u << 27)	/* Since: 1.3.3 */
#define FWUPD_DEVICE_FLAG_SELF_RECOVERY		(1u << 28)	/* Since: 1.3.3 */
#define FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE	(1u << 29)	/* Since: 1.3.3 */
#define FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED (1u << 30)	/* Since: 1.3.7 */
#define FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES	(1u << 31)	/* Since: 1.3.7 */
#define FWUPD_DEVICE_FLAG_MD_SET_NAME		(1llu << 32)	/* Since: 1.4.0; Deprecated: 1.5.5 */
#define FWUPD_DEVICE_FLAG_MD_SET_NAME_CATEGORY	(1llu << 33)	/* Since: 1.4.0; Deprecated: 1.5.5 */
#define FWUPD_DEVICE_FLAG_MD_SET_VERFMT		(1llu << 34)	/* Since: 1.4.0; Deprecated: 1.5.5 */
#define FWUPD_DEVICE_FLAG_ADD_COUNTERPART_GUIDS	(1llu << 35)	/* Since: 1.4.0 */
#define FWUPD_DEVICE_FLAG_NO_GUID_MATCHING	(1llu << 36)	/* Since: 1.4.1; Deprecated: 1.5.8 */
#define FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN	(1llu << 37)	/* Since: 1.4.1 */
#define FWUPD_DEVICE_FLAG_SKIPS_RESTART		(1llu << 38)	/* Since: 1.5.0 */
#define FWUPD_DEVICE_FLAG_HAS_MULTIPLE_BRANCHES	(1llu << 39)	/* Since: 1.5.0 */
#define FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL	(1llu << 40)	/* Since: 1.5.0 */
#define FWUPD_DEVICE_FLAG_MD_SET_ICON		(1llu << 41)	/* Since: 1.5.2; Deprecated: 1.5.5 */
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
 * @FWUPD_RELEASE_FLAG_IS_ALTERNATE_BRANCH:	Is an alternate branch of firmware
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
#define FWUPD_RELEASE_FLAG_IS_ALTERNATE_BRANCH	(1u << 6)	/* Since: 1.5.0 */
#define FWUPD_RELEASE_FLAG_UNKNOWN		G_MAXUINT64	/* Since: 1.2.6 */
typedef guint64 FwupdReleaseFlags;

/**
 * FwupdReleaseUrgency:
 * @FWUPD_RELEASE_URGENCY_UNKNOWN:		Unknown
 * @FWUPD_RELEASE_URGENCY_LOW:			Low
 * @FWUPD_RELEASE_URGENCY_MEDIUM:		Medium
 * @FWUPD_RELEASE_URGENCY_HIGH:			High
 * @FWUPD_RELEASE_URGENCY_CRITICAL:		Critical, e.g. a security fix
 *
 * The release urgency.
 **/
typedef enum {
	FWUPD_RELEASE_URGENCY_UNKNOWN,				/* Since: 1.4.0 */
	FWUPD_RELEASE_URGENCY_LOW,				/* Since: 1.4.0 */
	FWUPD_RELEASE_URGENCY_MEDIUM,				/* Since: 1.4.0 */
	FWUPD_RELEASE_URGENCY_HIGH,				/* Since: 1.4.0 */
	FWUPD_RELEASE_URGENCY_CRITICAL,				/* Since: 1.4.0 */
	/*< private >*/
	FWUPD_RELEASE_URGENCY_LAST
} FwupdReleaseUrgency;

/**
 * FwupdPluginFlags:
 * @FWUPD_PLUGIN_FLAG_NONE:			No flags set
 * @FWUPD_PLUGIN_FLAG_DISABLED:			Disabled
 * @FWUPD_PLUGIN_FLAG_USER_WARNING:		Show the user a warning
 * @FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE:		Clear the UPDATABLE flag from devices
 * @FWUPD_PLUGIN_FLAG_NO_HARDWARE:		No hardware is found
 * @FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED:	UEFI UpdateCapsule are unsupported
 * @FWUPD_PLUGIN_FLAG_UNLOCK_REQUIRED:		Hardware unlock is required
 * @FWUPD_PLUGIN_FLAG_EFIVAR_NOT_MOUNTED:	The efivar filesystem is not found
 * @FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND:		The EFI ESP not found
 * @FWUPD_PLUGIN_FLAG_LEGACY_BIOS:		System running in legacy CSM mode
 * @FWUPD_PLUGIN_FLAG_FAILED_OPEN:		Failed to open plugin (missing dependency)
 *
 * The plugin flags.
 **/
#define FWUPD_PLUGIN_FLAG_NONE			(0u)		/* Since: 1.5.0 */
#define FWUPD_PLUGIN_FLAG_DISABLED		(1u << 0)	/* Since: 1.5.0 */
#define FWUPD_PLUGIN_FLAG_USER_WARNING		(1u << 1)	/* Since: 1.5.0 */
#define FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE	(1u << 2)	/* Since: 1.5.0 */
#define FWUPD_PLUGIN_FLAG_NO_HARDWARE		(1u << 3)	/* Since: 1.5.0 */
#define FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED	(1u << 4)	/* Since: 1.5.0 */
#define FWUPD_PLUGIN_FLAG_UNLOCK_REQUIRED	(1u << 5)	/* Since: 1.5.0 */
#define FWUPD_PLUGIN_FLAG_EFIVAR_NOT_MOUNTED	(1u << 6)	/* Since: 1.5.0 */
#define FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND		(1u << 7)	/* Since: 1.5.0 */
#define FWUPD_PLUGIN_FLAG_LEGACY_BIOS		(1u << 8)	/* Since: 1.5.0 */
#define FWUPD_PLUGIN_FLAG_FAILED_OPEN		(1u << 9)	/* Since: 1.5.0 */
#define FWUPD_PLUGIN_FLAG_UNKNOWN		G_MAXUINT64	/* Since: 1.5.0 */
typedef guint64 FwupdPluginFlags;

/**
 * FwupdInstallFlags:
 * @FWUPD_INSTALL_FLAG_NONE:			No flags set
 * @FWUPD_INSTALL_FLAG_OFFLINE:			Schedule this for next boot
 * @FWUPD_INSTALL_FLAG_ALLOW_REINSTALL:		Allow reinstalling the same version
 * @FWUPD_INSTALL_FLAG_ALLOW_OLDER:		Allow downgrading firmware
 * @FWUPD_INSTALL_FLAG_FORCE:			Force the update even if not a good idea
 * @FWUPD_INSTALL_FLAG_NO_HISTORY:		Do not write to the history database
 * @FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH:	Allow firmware branch switching
 * @FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM:		Ignore firmware CRCs and checksums
 * @FWUPD_INSTALL_FLAG_IGNORE_VID_PID:		Ignore firmware vendor and project checks
 * @FWUPD_INSTALL_FLAG_IGNORE_POWER:		Ignore requirement of external power source
 * @FWUPD_INSTALL_FLAG_NO_SEARCH:		Do not use heuristics when parsing the image
 *
 * Flags to set when performing the firmware update or install.
 **/
typedef enum {
	FWUPD_INSTALL_FLAG_NONE			= 0,		/* Since: 0.7.0 */
	FWUPD_INSTALL_FLAG_OFFLINE		= 1 << 0,	/* Since: 0.7.0 */
	FWUPD_INSTALL_FLAG_ALLOW_REINSTALL	= 1 << 1,	/* Since: 0.7.0 */
	FWUPD_INSTALL_FLAG_ALLOW_OLDER		= 1 << 2,	/* Since: 0.7.0 */
	FWUPD_INSTALL_FLAG_FORCE		= 1 << 3,	/* Since: 0.7.1 */
	FWUPD_INSTALL_FLAG_NO_HISTORY		= 1 << 4,	/* Since: 1.0.8 */
	FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH	= 1 << 5,	/* Since: 1.5.0 */
	FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM	= 1 << 6,	/* Since: 1.5.0 */
	FWUPD_INSTALL_FLAG_IGNORE_VID_PID	= 1 << 7,	/* Since: 1.5.0 */
	FWUPD_INSTALL_FLAG_IGNORE_POWER		= 1 << 8,	/* Since: 1.5.0 */
	FWUPD_INSTALL_FLAG_NO_SEARCH		= 1 << 9,	/* Since: 1.5.0 */
	/*< private >*/
	FWUPD_INSTALL_FLAG_LAST
} FwupdInstallFlags;

/**
 * FwupdSelfSignFlags:
 * @FWUPD_SELF_SIGN_FLAG_NONE:			No flags set
 * @FWUPD_SELF_SIGN_FLAG_ADD_TIMESTAMP:		Add the timestamp to the detached signature
 * @FWUPD_SELF_SIGN_FLAG_ADD_CERT:		Add the certificate to the detached signature
 *
 * Flags to set when performing the firmware update or install.
 **/
typedef enum {
	FWUPD_SELF_SIGN_FLAG_NONE		= 0,		/* Since: 1.2.6 */
	FWUPD_SELF_SIGN_FLAG_ADD_TIMESTAMP	= 1 << 0,	/* Since: 1.2.6 */
	FWUPD_SELF_SIGN_FLAG_ADD_CERT		= 1 << 1,	/* Since: 1.2.6 */
	/*< private >*/
	FWUPD_SELF_SIGN_FLAG_LAST
} FwupdSelfSignFlags;

/**
 * FwupdUpdateState:
 * @FWUPD_UPDATE_STATE_UNKNOWN:			Unknown
 * @FWUPD_UPDATE_STATE_PENDING:			Update is pending
 * @FWUPD_UPDATE_STATE_SUCCESS:			Update was successful
 * @FWUPD_UPDATE_STATE_FAILED:			Update failed
 * @FWUPD_UPDATE_STATE_NEEDS_REBOOT:		Waiting for a reboot to apply
 * @FWUPD_UPDATE_STATE_FAILED_TRANSIENT:	Update failed due to transient issue, e.g. AC power required
 *
 * The update state.
 **/
typedef enum {
	FWUPD_UPDATE_STATE_UNKNOWN,			/* Since: 0.7.0 */
	FWUPD_UPDATE_STATE_PENDING,			/* Since: 0.7.0 */
	FWUPD_UPDATE_STATE_SUCCESS,			/* Since: 0.7.0 */
	FWUPD_UPDATE_STATE_FAILED,			/* Since: 0.7.0 */
	FWUPD_UPDATE_STATE_NEEDS_REBOOT,		/* Since: 1.0.4 */
	FWUPD_UPDATE_STATE_FAILED_TRANSIENT,		/* Since: 1.2.7 */
	/*< private >*/
	FWUPD_UPDATE_STATE_LAST
} FwupdUpdateState;

/**
 * FwupdKeyringKind:
 * @FWUPD_KEYRING_KIND_UNKNOWN:			Unknown
 * @FWUPD_KEYRING_KIND_NONE:			No verification
 * @FWUPD_KEYRING_KIND_GPG:			Verification using GPG
 * @FWUPD_KEYRING_KIND_PKCS7:			Verification using PKCS7
 * @FWUPD_KEYRING_KIND_JCAT:			Verification using Jcat
 *
 * The update state.
 **/
typedef enum {
	FWUPD_KEYRING_KIND_UNKNOWN,			/* Since: 0.9.7 */
	FWUPD_KEYRING_KIND_NONE,			/* Since: 0.9.7 */
	FWUPD_KEYRING_KIND_GPG,				/* Since: 0.9.7 */
	FWUPD_KEYRING_KIND_PKCS7,			/* Since: 0.9.7 */
	FWUPD_KEYRING_KIND_JCAT,			/* Since: 1.4.0 */
	/*< private >*/
	FWUPD_KEYRING_KIND_LAST
} FwupdKeyringKind;

/**
 * FwupdVersionFormat:
 * @FWUPD_VERSION_FORMAT_UNKNOWN:		Unknown version format
 * @FWUPD_VERSION_FORMAT_PLAIN:			An unidentified format text string
 * @FWUPD_VERSION_FORMAT_NUMBER:		A single integer version number
 * @FWUPD_VERSION_FORMAT_PAIR:			Two AABB.CCDD version numbers
 * @FWUPD_VERSION_FORMAT_TRIPLET:		Microsoft-style AA.BB.CCDD version numbers
 * @FWUPD_VERSION_FORMAT_QUAD:			UEFI-style AA.BB.CC.DD version numbers
 * @FWUPD_VERSION_FORMAT_BCD:			Binary coded decimal notation
 * @FWUPD_VERSION_FORMAT_INTEL_ME:		Intel ME-style bitshifted notation
 * @FWUPD_VERSION_FORMAT_INTEL_ME2:		Intel ME-style A.B.CC.DDDD notation notation
 * @FWUPD_VERSION_FORMAT_SURFACE_LEGACY:	Legacy Microsoft Surface 10b.12b.10b
 * @FWUPD_VERSION_FORMAT_SURFACE:		Microsoft Surface 8b.16b.8b
 * @FWUPD_VERSION_FORMAT_DELL_BIOS:		Dell BIOS BB.CC.DD style
 * @FWUPD_VERSION_FORMAT_HEX:			Hexadecimal 0xAABCCDD style
 *
 * The flags used when parsing version numbers.
 *
 * If no verification is required then %FWUPD_VERSION_FORMAT_PLAIN should
 * be used to signify an unparsable text string.
 **/
typedef enum {
	FWUPD_VERSION_FORMAT_UNKNOWN,			/* Since: 1.2.9 */
	FWUPD_VERSION_FORMAT_PLAIN,			/* Since: 1.2.9 */
	FWUPD_VERSION_FORMAT_NUMBER,			/* Since: 1.2.9 */
	FWUPD_VERSION_FORMAT_PAIR,			/* Since: 1.2.9 */
	FWUPD_VERSION_FORMAT_TRIPLET,			/* Since: 1.2.9 */
	FWUPD_VERSION_FORMAT_QUAD,			/* Since: 1.2.9 */
	FWUPD_VERSION_FORMAT_BCD,			/* Since: 1.2.9 */
	FWUPD_VERSION_FORMAT_INTEL_ME,			/* Since: 1.2.9 */
	FWUPD_VERSION_FORMAT_INTEL_ME2,			/* Since: 1.2.9 */
	FWUPD_VERSION_FORMAT_SURFACE_LEGACY,		/* Since: 1.3.4 */
	FWUPD_VERSION_FORMAT_SURFACE,			/* Since: 1.3.4 */
	FWUPD_VERSION_FORMAT_DELL_BIOS,			/* Since: 1.3.6 */
	FWUPD_VERSION_FORMAT_HEX,		/* Since: 1.4.0 */
	/*< private >*/
	FWUPD_VERSION_FORMAT_LAST
} FwupdVersionFormat;

const gchar	*fwupd_status_to_string			(FwupdStatus	 status);
FwupdStatus	 fwupd_status_from_string		(const gchar	*status);
const gchar	*fwupd_device_flag_to_string		(FwupdDeviceFlags device_flag);
FwupdDeviceFlags fwupd_device_flag_from_string		(const gchar	*device_flag);
const gchar	*fwupd_plugin_flag_to_string		(FwupdPluginFlags plugin_flag);
FwupdPluginFlags fwupd_plugin_flag_from_string		(const gchar	*plugin_flag);
const gchar	*fwupd_release_flag_to_string		(FwupdReleaseFlags release_flag);
FwupdReleaseFlags fwupd_release_flag_from_string	(const gchar	*release_flag);
const gchar	*fwupd_release_urgency_to_string	(FwupdReleaseUrgency release_urgency);
FwupdReleaseUrgency fwupd_release_urgency_from_string	(const gchar	*release_urgency);
const gchar	*fwupd_update_state_to_string		(FwupdUpdateState update_state);
FwupdUpdateState fwupd_update_state_from_string		(const gchar	*update_state);
const gchar	*fwupd_trust_flag_to_string		(FwupdTrustFlags trust_flag);
FwupdTrustFlags	 fwupd_trust_flag_from_string		(const gchar	*trust_flag);
const gchar	*fwupd_feature_flag_to_string		(FwupdFeatureFlags feature_flag);
FwupdFeatureFlags fwupd_feature_flag_from_string	(const gchar	*feature_flag);
FwupdKeyringKind fwupd_keyring_kind_from_string		(const gchar	*keyring_kind);
const gchar	*fwupd_keyring_kind_to_string		(FwupdKeyringKind keyring_kind);
FwupdVersionFormat fwupd_version_format_from_string	(const gchar	*str);
const gchar	*fwupd_version_format_to_string		(FwupdVersionFormat kind);

G_END_DECLS
