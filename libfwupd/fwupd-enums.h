/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/**
 * FwupdStatus:
 *
 * The flags to show daemon status.
 **/
typedef enum {
	/**
	 * FWUPD_STATUS_UNKNOWN:
	 *
	 * Unknown state.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_STATUS_UNKNOWN,
	/**
	 * FWUPD_STATUS_IDLE:
	 *
	 * Idle.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_STATUS_IDLE,
	/**
	 * FWUPD_STATUS_LOADING:
	 *
	 * Loading a resource.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_STATUS_LOADING,
	/**
	 * FWUPD_STATUS_DECOMPRESSING:
	 *
	 * Decompressing firmware.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_STATUS_DECOMPRESSING,
	/**
	 * FWUPD_STATUS_DEVICE_RESTART:
	 *
	 * Restarting the device.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_STATUS_DEVICE_RESTART,
	/**
	 * FWUPD_STATUS_DEVICE_WRITE:
	 *
	 * Writing to a device.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_STATUS_DEVICE_WRITE,
	/**
	 * FWUPD_STATUS_DEVICE_VERIFY:
	 *
	 * Verifying (reading) a device.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_STATUS_DEVICE_VERIFY,
	/**
	 * FWUPD_STATUS_SCHEDULING:
	 *
	 * Scheduling an update for installation on reboot.
	 *
	 * Since: 0.1.1
	 */
	FWUPD_STATUS_SCHEDULING,
	/**
	 * FWUPD_STATUS_DOWNLOADING:
	 *
	 * A file is downloading.
	 *
	 * Since: 0.9.4
	 */
	FWUPD_STATUS_DOWNLOADING,
	/**
	 * FWUPD_STATUS_DEVICE_READ:
	 *
	 * Reading from a device.
	 *
	 * Since: 1.0.0
	 */
	FWUPD_STATUS_DEVICE_READ,
	/**
	 * FWUPD_STATUS_DEVICE_ERASE:
	 *
	 * Erasing a device.
	 *
	 * Since: 1.0.0
	 */
	FWUPD_STATUS_DEVICE_ERASE,
	/**
	 * FWUPD_STATUS_WAITING_FOR_AUTH:
	 *
	 * Waiting for authentication.
	 *
	 * Since: 1.0.0
	 */
	FWUPD_STATUS_WAITING_FOR_AUTH,
	/**
	 * FWUPD_STATUS_DEVICE_BUSY:
	 *
	 * The device is busy.
	 *
	 * Since: 1.0.1
	 */
	FWUPD_STATUS_DEVICE_BUSY,
	/**
	 * FWUPD_STATUS_SHUTDOWN:
	 *
	 * The daemon is shutting down.
	 *
	 * Since: 1.2.1
	 */
	FWUPD_STATUS_SHUTDOWN,
	/**
	 * FWUPD_STATUS_WAITING_FOR_USER:
	 *
	 * Waiting for an interactive user action.
	 *
	 * Since: 1.9.8
	 */
	FWUPD_STATUS_WAITING_FOR_USER,
	/*< private >*/
	FWUPD_STATUS_LAST
} FwupdStatus;

/**
 * FwupdFeatureFlags:
 *
 * The flags to the feature capabilities of the front-end client.
 **/
typedef enum {
	/**
	 * FWUPD_FEATURE_FLAG_NONE:
	 *
	 * No trust.
	 *
	 * Since: 1.4.5
	 */
	FWUPD_FEATURE_FLAG_NONE = 0,
	/**
	 * FWUPD_FEATURE_FLAG_CAN_REPORT:
	 *
	 * Can upload a report of the update back to the server.
	 *
	 * Since: 1.4.5
	 */
	FWUPD_FEATURE_FLAG_CAN_REPORT = 1 << 0,
	/**
	 * FWUPD_FEATURE_FLAG_DETACH_ACTION:
	 *
	 * Can perform detach action, typically showing text.
	 *
	 * Since: 1.4.5
	 */
	FWUPD_FEATURE_FLAG_DETACH_ACTION = 1 << 1,
	/**
	 * FWUPD_FEATURE_FLAG_UPDATE_ACTION:
	 *
	 * Can perform update action, typically showing text.
	 *
	 * Since: 1.4.5
	 */
	FWUPD_FEATURE_FLAG_UPDATE_ACTION = 1 << 2,
	/**
	 * FWUPD_FEATURE_FLAG_SWITCH_BRANCH:
	 *
	 * Can switch the firmware branch.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_FEATURE_FLAG_SWITCH_BRANCH = 1 << 3,
	/**
	 * FWUPD_FEATURE_FLAG_REQUESTS:
	 *
	 * Can show interactive requests.
	 *
	 * Since: 1.6.2
	 */
	FWUPD_FEATURE_FLAG_REQUESTS = 1 << 4,
	/**
	 * FWUPD_FEATURE_FLAG_FDE_WARNING:
	 *
	 * Can warn about full disk encryption.
	 *
	 * Since: 1.7.1
	 */
	FWUPD_FEATURE_FLAG_FDE_WARNING = 1 << 5,
	/**
	 * FWUPD_FEATURE_FLAG_COMMUNITY_TEXT:
	 *
	 * Can show information about community supported.
	 *
	 * Since: 1.7.5
	 */
	FWUPD_FEATURE_FLAG_COMMUNITY_TEXT = 1 << 6,
	/**
	 * FWUPD_FEATURE_FLAG_SHOW_PROBLEMS:
	 *
	 * Can show problems when getting the update list.
	 *
	 * Since: 1.8.1
	 */
	FWUPD_FEATURE_FLAG_SHOW_PROBLEMS = 1 << 7,
	/**
	 * FWUPD_FEATURE_FLAG_ALLOW_AUTHENTICATION:
	 *
	 * Can authenticate with PolicyKit for requests.
	 *
	 * Since: 1.8.4
	 */
	FWUPD_FEATURE_FLAG_ALLOW_AUTHENTICATION = 1 << 8,
	/**
	 * FWUPD_FEATURE_FLAG_REQUESTS_NON_GENERIC:
	 *
	 * Can handle showing non-generic request message text.
	 *
	 * Since: 1.9.8
	 */
	FWUPD_FEATURE_FLAG_REQUESTS_NON_GENERIC = 1 << 9, /* Since: 1.9.8 */
	/*< private >*/
	FWUPD_FEATURE_FLAG_UNKNOWN = G_MAXUINT64,
} FwupdFeatureFlags;

/**
 * FwupdDeviceFlags:
 *
 * Flags used to represent device attributes
 */
typedef enum {
	/**
	 * FWUPD_DEVICE_FLAG_NONE:
	 *
	 * No flags set
	 *
	 * Since: 0.1.3
	 */
	FWUPD_DEVICE_FLAG_NONE = 0ull,
	/**
	 * FWUPD_DEVICE_FLAG_INTERNAL:
	 *
	 * Device is internal to the platform and cannot be removed easily.
	 *
	 * Since: 0.1.3
	 */
	FWUPD_DEVICE_FLAG_INTERNAL = 1ull << 0,
	/**
	 * FWUPD_DEVICE_FLAG_UPDATABLE:
	 *
	 * Device has the ability to be updated in this or any other mode.
	 *
	 * Since: 0.9.7
	 */
	FWUPD_DEVICE_FLAG_UPDATABLE = 1ull << 1,
	/**
	 * FWUPD_DEVICE_FLAG_REQUIRE_AC:
	 *
	 * Device requires an external power source to be connected or the battery
	 * level at a minimum threshold to update.
	 *
	 * Since: 0.6.3
	 */
	FWUPD_DEVICE_FLAG_REQUIRE_AC = 1ull << 3,
	/**
	 * FWUPD_DEVICE_FLAG_LOCKED:
	 *
	 * The device can not be updated without manual user interaction.
	 *
	 * Since: 0.6.3
	 */
	FWUPD_DEVICE_FLAG_LOCKED = 1ull << 4,
	/**
	 * FWUPD_DEVICE_FLAG_SUPPORTED:
	 *
	 * The device is found in metadata loaded into the daemon.
	 *
	 * Since: 0.7.1
	 */
	FWUPD_DEVICE_FLAG_SUPPORTED = 1ull << 5,
	/**
	 * FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER:
	 *
	 * The device requires entering a bootloader mode to be manually.
	 *
	 * Since: 0.7.3
	 */
	FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER = 1ull << 6,
	/**
	 * FWUPD_DEVICE_FLAG_NEEDS_REBOOT:
	 *
	 * The device requires a system reboot to apply firmware or to reload hardware.
	 *
	 * Since: 0.9.7
	 */
	FWUPD_DEVICE_FLAG_NEEDS_REBOOT = 1ull << 8,
	/**
	 * FWUPD_DEVICE_FLAG_REPORTED:
	 *
	 * The success or failure of a previous update has been reported to a metadata server.
	 *
	 * Since: 1.0.4
	 */
	FWUPD_DEVICE_FLAG_REPORTED = 1ull << 9,
	/**
	 * FWUPD_DEVICE_FLAG_NOTIFIED:
	 *
	 * The user has been notified about a change in the device state.
	 *
	 * Since: 1.0.5
	 */
	FWUPD_DEVICE_FLAG_NOTIFIED = 1ull << 10,
	/**
	 * FWUPD_DEVICE_FLAG_IS_BOOTLOADER:
	 *
	 * The device is currently in a read-only bootloader mode and not running application code.
	 *
	 * Since: 1.0.8
	 */
	FWUPD_DEVICE_FLAG_IS_BOOTLOADER = 1ull << 13,
	/**
	 * FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG:
	 *
	 * The device is in the middle of and update and the hardware is waiting to be probed or
	 * replugged.
	 *
	 * Since: 1.1.2
	 */
	FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG = 1ull << 14,
	/**
	 * FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN:
	 *
	 * The device requires the system to be shutdown to finish application of new firmware.
	 *
	 * Since: 1.2.4
	 */
	FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN = 1ull << 17,
	/**
	 * FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED:
	 *
	 * The device requires the update to be retried, possibly with a different plugin.
	 *
	 * Since: 1.2.5
	 */
	FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED = 1ull << 18,
	/**
	 * FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION:
	 *
	 * The device update needs to be separately activated.
	 * This process may occur automatically on shutdown in some operating systems or when the
	 * device is unplugged with some devices.
	 *
	 * Since: 1.2.6
	 */
	FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION = 1ull << 20,
	/**
	 * FWUPD_DEVICE_FLAG_HISTORICAL:
	 *
	 * The device is used for historical data only.
	 *
	 * Since: 1.3.2
	 */
	FWUPD_DEVICE_FLAG_HISTORICAL = 1ull << 22,
	/**
	 * FWUPD_DEVICE_FLAG_WILL_DISAPPEAR:
	 *
	 * The device will disappear after the update is complete and success or failure can't be
	 * verified.
	 *
	 * Since: 1.3.3
	 */
	FWUPD_DEVICE_FLAG_WILL_DISAPPEAR = 1ull << 24,
	/**
	 * FWUPD_DEVICE_FLAG_CAN_VERIFY:
	 *
	 * The device checksums can be compared against metadata.
	 *
	 * Since: 1.3.3
	 */
	FWUPD_DEVICE_FLAG_CAN_VERIFY = 1ull << 25,
	/**
	 * FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE:
	 *
	 * The device application firmware image can be dumped from device for verification.
	 *
	 * Since: 1.3.3
	 */
	FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE = 1ull << 26,
	/**
	 * FWUPD_DEVICE_FLAG_DUAL_IMAGE:
	 *
	 * The device firmware update architecture uses a redundancy mechanism such as A/B
	 * partitions for updates.
	 *
	 * Since: 1.3.3
	 */
	FWUPD_DEVICE_FLAG_DUAL_IMAGE = 1ull << 27,
	/**
	 * FWUPD_DEVICE_FLAG_SELF_RECOVERY:
	 *
	 * In flashing mode, the device will only accept intended payloads and will revert back to
	 * a valid firmware image if an invalid or incomplete payload was sent.
	 *
	 * Since: 1.3.3
	 */
	FWUPD_DEVICE_FLAG_SELF_RECOVERY = 1ull << 28,
	/**
	 * FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE:
	 *
	 * The device remains usable while the update flashes or schedules the update.
	 * The update will implicitly be applied next time the device is power cycled or possibly
	 * activated.
	 *
	 * Since: 1.3.3
	 */
	FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE = 1ull << 29,
	/**
	 * FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED:
	 *
	 * All firmware updates for this device require a firmware version check.
	 *
	 * Since: 1.3.7
	 */
	FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED = 1ull << 30,
	/**
	 * FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES:
	 *
	 * Install each intermediate releases for the device rather than jumping directly to the
	 * newest.
	 *
	 * Since: 1.3.7
	 */
	FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES = 1ull << 31,
	/**
	 * FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN:
	 *
	 * The device is updatable but is currently inhibited from updates in the client.
	 * Reasons include but are not limited to low power or requiring reboot from a previous
	 * update.
	 *
	 * Since: 1.4.1
	 */
	FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN = 1ull << 37,
	/**
	 * FWUPD_DEVICE_FLAG_HAS_MULTIPLE_BRANCHES:
	 *
	 * The device supports switching to a different stream of firmware.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_DEVICE_FLAG_HAS_MULTIPLE_BRANCHES = 1ull << 39,
	/**
	 * FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL:
	 *
	 * The device firmware should be saved before installing firmware.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL = 1ull << 40,
	/**
	 * FWUPD_DEVICE_FLAG_WILDCARD_INSTALL:
	 *
	 * All devices with matching GUIDs will be updated at the same time.
	 *
	 * For some devices it is not possible to have different versions of firmware
	 * for hardware of the same type. Updating one device will force update of
	 * others with exactly the same instance IDs.
	 *
	 * Since: 1.6.2
	 */
	FWUPD_DEVICE_FLAG_WILDCARD_INSTALL = 1ull << 42,
	/**
	 * FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE:
	 *
	 * The device firmware can only be updated to a newer version and never downgraded or
	 * reinstalled.
	 *
	 * Since: 1.6.2
	 */
	FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE = 1ull << 43,
	/**
	 * FWUPD_DEVICE_FLAG_UNREACHABLE:
	 *
	 * The device is currently unreachable, perhaps because it is in a lower power state or is
	 * out of wireless range.
	 *
	 * Since: 1.7.0
	 */
	FWUPD_DEVICE_FLAG_UNREACHABLE = 1ull << 44,
	/**
	 * FWUPD_DEVICE_FLAG_AFFECTS_FDE:
	 *
	 * The device is warning that a volume with full-disk-encryption was found on this machine,
	 * typically a Windows NTFS partition with BitLocker.
	 * Updating the firmware on this device may invalidate secrets used to decrypt the volume,
	 * and the recovery key may be required.
	 *
	 * Supported clients will display this information as a warning to the user.
	 *
	 * Since: 1.7.1
	 */
	FWUPD_DEVICE_FLAG_AFFECTS_FDE = 1ull << 45,
	/**
	 * FWUPD_DEVICE_FLAG_END_OF_LIFE:
	 *
	 * The device is no longer supported by the original hardware vendor as it is considered
	 * end-of-life. It it unlikely to receive firmware updates, even for security issues.
	 *
	 * Since: 1.7.5
	 */
	FWUPD_DEVICE_FLAG_END_OF_LIFE = 1ull << 46,
	/**
	 * FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD:
	 *
	 * The firmware payload is verified on-device the payload using strong cryptography such
	 * as RSA, AES or ECC.
	 *
	 * It is usually not possible to modify or flash custom firmware not provided by the vendor.
	 *
	 * Since: 1.7.6
	 */
	FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD = 1ull << 47,
	/**
	 * FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD:
	 *
	 * The firmware payload is unsigned and it is possible to modify and flash custom firmware.
	 *
	 * Since: 1.7.6
	 */
	FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD = 1ull << 48,
	/**
	 * FWUPD_DEVICE_FLAG_EMULATED:
	 *
	 * The device is emulated and should not be recorded by the backend.
	 *
	 * Since: 1.8.11
	 */
	FWUPD_DEVICE_FLAG_EMULATED = 1ull << 49,
	/**
	 * FWUPD_DEVICE_FLAG_EMULATION_TAG:
	 *
	 * The device should be recorded by the backend, allowing emulation.
	 *
	 * Since: 1.8.11
	 */
	FWUPD_DEVICE_FLAG_EMULATION_TAG = 1ull << 50,
	/**
	 * FWUPD_DEVICE_FLAG_ONLY_EXPLICIT_UPDATES:
	 *
	 * The device should stay on one firmware version unless the new version is explicitly
	 * specified.
	 *
	 * This can either be done using `fwupdmgr install`, using GNOME Firmware, or using a BKC
	 * config.
	 *
	 * Since: 1.9.3
	 */
	FWUPD_DEVICE_FLAG_ONLY_EXPLICIT_UPDATES = 1ull << 51,
	/**
	 * FWUPD_DEVICE_FLAG_UNKNOWN:
	 *
	 * This flag is not defined, this typically will happen from mismatched fwupd library and
	 * clients.
	 *
	 * Since: 0.7.3
	 */
	/*< private >*/
	FWUPD_DEVICE_FLAG_UNKNOWN = G_MAXUINT64,
} FwupdDeviceFlags;

/**
 * FwupdDeviceProblem:
 *
 * Problems are reasons why the device is not updatable.
 *
 * All problems have to be fixable by the user, rather than the plugin author.
 */
typedef enum {
	/**
	 * FWUPD_DEVICE_PROBLEM_NONE:
	 *
	 * No device problems detected.
	 *
	 * Since: 1.8.1
	 */
	FWUPD_DEVICE_PROBLEM_NONE = 0u,
	/**
	 * FWUPD_DEVICE_PROBLEM_SYSTEM_POWER_TOO_LOW:
	 *
	 * The system power is too low to perform the update.
	 *
	 * Since: 1.8.1
	 */
	FWUPD_DEVICE_PROBLEM_SYSTEM_POWER_TOO_LOW = 1ull << 0,
	/**
	 * FWUPD_DEVICE_PROBLEM_UNREACHABLE:
	 *
	 * The device is unreachable, or out of wireless range.
	 *
	 * Since: 1.8.1
	 */
	FWUPD_DEVICE_PROBLEM_UNREACHABLE = 1ull << 1,
	/**
	 * FWUPD_DEVICE_PROBLEM_POWER_TOO_LOW:
	 *
	 * The device battery power is too low.
	 *
	 * Since: 1.8.1
	 */
	FWUPD_DEVICE_PROBLEM_POWER_TOO_LOW = 1ull << 2,
	/**
	 * FWUPD_DEVICE_PROBLEM_UPDATE_PENDING:
	 *
	 * The device is waiting for the update to be applied.
	 *
	 * Since: 1.8.1
	 */
	FWUPD_DEVICE_PROBLEM_UPDATE_PENDING = 1ull << 3,
	/**
	 * FWUPD_DEVICE_PROBLEM_REQUIRE_AC_POWER:
	 *
	 * The device requires AC power to be connected.
	 *
	 * Since: 1.8.1
	 */
	FWUPD_DEVICE_PROBLEM_REQUIRE_AC_POWER = 1ull << 4,
	/**
	 * FWUPD_DEVICE_PROBLEM_LID_IS_CLOSED:
	 *
	 * The device cannot be used while the laptop lid is closed.
	 *
	 * Since: 1.8.1
	 */
	FWUPD_DEVICE_PROBLEM_LID_IS_CLOSED = 1ull << 5,
	/**
	 * FWUPD_DEVICE_PROBLEM_IS_EMULATED:
	 *
	 * The device is emulated from a different host.
	 *
	 * Since: 1.8.3
	 */
	FWUPD_DEVICE_PROBLEM_IS_EMULATED = 1ull << 6,
	/**
	 * FWUPD_DEVICE_PROBLEM_MISSING_LICENSE:
	 *
	 * The device cannot be updated due to missing vendor's license.
	 *
	 * Since: 1.8.6
	 */
	FWUPD_DEVICE_PROBLEM_MISSING_LICENSE = 1ull << 7,
	/**
	 * FWUPD_DEVICE_PROBLEM_SYSTEM_INHIBIT:
	 *
	 * The device cannot be updated due to a system-wide inhibit.
	 *
	 * Since: 1.8.10
	 */
	FWUPD_DEVICE_PROBLEM_SYSTEM_INHIBIT = 1ull << 8,
	/**
	 * FWUPD_DEVICE_PROBLEM_UPDATE_IN_PROGRESS:
	 *
	 * The device cannot be updated as it is already being updated.
	 *
	 * Since: 1.8.11
	 */
	FWUPD_DEVICE_PROBLEM_UPDATE_IN_PROGRESS = 1ull << 9,
	/**
	 * FWUPD_DEVICE_PROBLEM_IN_USE:
	 *
	 * The device is in use and cannot be interrupted, for instance taking a phone call.
	 *
	 * Since: 1.9.1
	 */
	FWUPD_DEVICE_PROBLEM_IN_USE = 1ull << 10,
	/**
	 * FWUPD_DEVICE_PROBLEM_DISPLAY_REQUIRED:
	 *
	 * The device cannot be used while there are no displays plugged in.
	 *
	 * Since: 1.9.6
	 */
	FWUPD_DEVICE_PROBLEM_DISPLAY_REQUIRED = 1ull << 11,
	/**
	 * FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY:
	 *
	 * We have two ways of communicating with one physical device, so we hide the worse one.
	 *
	 * Since: 2.0.0
	 */
	FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY = 1ull << 12,
	/**
	 * FWUPD_DEVICE_PROBLEM_UNKNOWN:
	 *
	 * This problem is not defined, this typically will happen from mismatched
	 * fwupd library and clients.
	 *
	 * Since: 1.8.1
	 */
	FWUPD_DEVICE_PROBLEM_UNKNOWN = G_MAXUINT64,
} FwupdDeviceProblem;

/**
 * FwupdReleaseFlags:
 *
 * Flags used to represent release attributes
 */
typedef enum {
	/**
	 * FWUPD_RELEASE_FLAG_NONE:
	 *
	 * No flags are set.
	 *
	 * Since: 1.2.6
	 */
	FWUPD_RELEASE_FLAG_NONE = 0u,
	/**
	 * FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD:
	 *
	 * The payload binary is trusted.
	 *
	 * Since: 1.2.6
	 */
	FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD = 1ull << 0,
	/**
	 * FWUPD_RELEASE_FLAG_TRUSTED_METADATA:
	 *
	 * The payload metadata is trusted.
	 *
	 * Since: 1.2.6
	 */
	FWUPD_RELEASE_FLAG_TRUSTED_METADATA = 1ull << 1,
	/**
	 * FWUPD_RELEASE_FLAG_IS_UPGRADE:
	 *
	 * The release is newer than the device version.
	 *
	 * Since: 1.2.6
	 */
	FWUPD_RELEASE_FLAG_IS_UPGRADE = 1ull << 2,
	/**
	 * FWUPD_RELEASE_FLAG_IS_DOWNGRADE:
	 *
	 * The release is older than the device version.
	 *
	 * Since: 1.2.6
	 */
	FWUPD_RELEASE_FLAG_IS_DOWNGRADE = 1ull << 3,
	/**
	 * FWUPD_RELEASE_FLAG_BLOCKED_VERSION:
	 *
	 * The installation of the release is blocked as below device version-lowest.
	 *
	 * Since: 1.2.6
	 */
	FWUPD_RELEASE_FLAG_BLOCKED_VERSION = 1ull << 4,
	/**
	 * FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL:
	 *
	 * The installation of the release is blocked as release not approved by an administrator.
	 *
	 * Since: 1.2.6
	 */
	FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL = 1ull << 5,
	/**
	 * FWUPD_RELEASE_FLAG_IS_ALTERNATE_BRANCH:
	 *
	 * The release is an alternate branch of firmware.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_RELEASE_FLAG_IS_ALTERNATE_BRANCH = 1ull << 6,
	/**
	 * FWUPD_RELEASE_FLAG_IS_COMMUNITY:
	 *
	 * The release is supported by the community and not the hardware vendor.
	 *
	 * Since: 1.7.5
	 */
	FWUPD_RELEASE_FLAG_IS_COMMUNITY = 1ull << 7,
	/**
	 * FWUPD_RELEASE_FLAG_TRUSTED_REPORT:
	 *
	 * The payload has been tested by a report we trust.
	 *
	 * Since: 1.9.1
	 */
	FWUPD_RELEASE_FLAG_TRUSTED_REPORT = 1ull << 8,
	/**
	 * FWUPD_RELEASE_FLAG_UNKNOWN:
	 *
	 * The release flag is unknown, typically caused by using mismatched client and daemon.
	 *
	 * Since: 1.2.6
	 */
	FWUPD_RELEASE_FLAG_UNKNOWN = G_MAXUINT64,
} FwupdReleaseFlags;

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
	FWUPD_RELEASE_URGENCY_UNKNOWN,	/* Since: 1.4.0 */
	FWUPD_RELEASE_URGENCY_LOW,	/* Since: 1.4.0 */
	FWUPD_RELEASE_URGENCY_MEDIUM,	/* Since: 1.4.0 */
	FWUPD_RELEASE_URGENCY_HIGH,	/* Since: 1.4.0 */
	FWUPD_RELEASE_URGENCY_CRITICAL, /* Since: 1.4.0 */
	/*< private >*/
	FWUPD_RELEASE_URGENCY_LAST
} FwupdReleaseUrgency;

/**
 * FwupdPluginFlags:
 *
 * Flags used to represent plugin attributes
 */
typedef enum {
	/**
	 * FWUPD_PLUGIN_FLAG_NONE:
	 *
	 * No plugin flags are set.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_PLUGIN_FLAG_NONE = 0u,
	/**
	 * FWUPD_PLUGIN_FLAG_DISABLED:
	 *
	 * The plugin has been disabled, either by daemon configuration or a problem.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_PLUGIN_FLAG_DISABLED = 1ull << 0,
	/**
	 * FWUPD_PLUGIN_FLAG_USER_WARNING:
	 *
	 * The plugin has a problem and would like to show a user warning to a supported client.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_PLUGIN_FLAG_USER_WARNING = 1ull << 1,
	/**
	 * FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE:
	 *
	 * When the plugin loads it should clear the UPDATABLE flag from any devices.
	 * This typically happens when the device requires a system restart.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE = 1ull << 2,
	/**
	 * FWUPD_PLUGIN_FLAG_NO_HARDWARE:
	 *
	 * The plugin won't load because no supported hardware was found.
	 * This typically happens with plugins designed for a specific platform design
	 * = such as the dell plugin only works on Dell systems,.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_PLUGIN_FLAG_NO_HARDWARE = 1ull << 3,
	/**
	 * FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED:
	 *
	 * The plugin discovered that UEFI UpdateCapsule are unsupported.
	 * Supported clients will display this information to a user.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED = 1ull << 4,
	/**
	 * FWUPD_PLUGIN_FLAG_UNLOCK_REQUIRED:
	 *
	 * The plugin discovered that hardware unlock is required.
	 * Supported clients will display this information to a user.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_PLUGIN_FLAG_UNLOCK_REQUIRED = 1ull << 5,
	/**
	 * FWUPD_PLUGIN_FLAG_EFIVAR_NOT_MOUNTED:
	 *
	 * The plugin discovered the efivar filesystem is not found and is required for this plugin.
	 * Supported clients will display this information to a user.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_PLUGIN_FLAG_EFIVAR_NOT_MOUNTED = 1ull << 6,
	/**
	 * FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND:
	 *
	 * The plugins discovered that the EFI system partition was not found.
	 * Supported clients will display this information to a user.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND = 1ull << 7,
	/**
	 * FWUPD_PLUGIN_FLAG_LEGACY_BIOS:
	 *
	 * The plugin discovered the system is running in legacy CSM mode.
	 * Supported clients will display this information to a user.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_PLUGIN_FLAG_LEGACY_BIOS = 1ull << 8,
	/**
	 * FWUPD_PLUGIN_FLAG_FAILED_OPEN:
	 *
	 * Failed to open plugin = missing dependency,.
	 * Supported clients will display this information to a user.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_PLUGIN_FLAG_FAILED_OPEN = 1ull << 9,
	/**
	 * FWUPD_PLUGIN_FLAG_REQUIRE_HWID:
	 *
	 * A specific HWID is required to use this plugin.
	 *
	 * Since: 1.5.8
	 */
	FWUPD_PLUGIN_FLAG_REQUIRE_HWID = 1ull << 10,
	/**
	 * FWUPD_PLUGIN_FLAG_KERNEL_TOO_OLD:
	 *
	 * The feature is not supported as the kernel is too old.
	 *
	 * Since: 1.6.2
	 */
	FWUPD_PLUGIN_FLAG_KERNEL_TOO_OLD = 1ull << 11,
	/**
	 * FWUPD_PLUGIN_FLAG_AUTH_REQUIRED:
	 *
	 * The plugin requires the user to provide authentication details.
	 * Supported clients will display this information to a user.
	 *
	 * Since: 1.6.2
	 */
	FWUPD_PLUGIN_FLAG_AUTH_REQUIRED = 1ull << 12,
	/**
	 * FWUPD_PLUGIN_FLAG_SECURE_CONFIG:
	 *
	 * The plugin requires the config file to be saved with permissions that only allow the
	 * root user to read.
	 *
	 * Since: 1.8.5
	 */
	FWUPD_PLUGIN_FLAG_SECURE_CONFIG = 1ull << 13,
	/**
	 * FWUPD_PLUGIN_FLAG_MODULAR:
	 *
	 * The plugin is loaded from an external module.
	 *
	 * Since: 1.8.6
	 */
	FWUPD_PLUGIN_FLAG_MODULAR = 1ull << 14,
	/**
	 * FWUPD_PLUGIN_FLAG_MEASURE_SYSTEM_INTEGRITY:
	 *
	 * The plugin will be checked that it preserves system state such as `KEK`, `PK`,
	 * `BOOT####` etc.
	 *
	 * Since: 1.8.7
	 */
	FWUPD_PLUGIN_FLAG_MEASURE_SYSTEM_INTEGRITY = 1ull << 15,
	/**
	 * FWUPD_PLUGIN_FLAG_ESP_NOT_VALID:
	 *
	 * The plugins discovered that the EFI system partition may not be valid.
	 * Supported clients will display this information to a user.
	 *
	 * Since: 1.9.3
	 */
	FWUPD_PLUGIN_FLAG_ESP_NOT_VALID = 1ull << 16,
	/**
	 * FWUPD_PLUGIN_FLAG_READY:
	 *
	 * The plugin is ready for use and all devices have been coldplugged.
	 *
	 * Since: 1.9.6
	 */
	FWUPD_PLUGIN_FLAG_READY = 1ull << 17,
	/**
	 * FWUPD_PLUGIN_FLAG_TEST_ONLY:
	 *
	 * The plugin is used for virtual devices that exercising daemon flows.
	 *
	 * Since: 2.0.0
	 */
	FWUPD_PLUGIN_FLAG_TEST_ONLY = 1ull << 18,
	/**
	 * FWUPD_PLUGIN_FLAG_UNKNOWN:
	 *
	 * The plugin flag is unknown.
	 * This is usually caused by a mismatched libfwupdplugin and daemon.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_PLUGIN_FLAG_UNKNOWN = G_MAXUINT64
} FwupdPluginFlags;

/**
 * FwupdInstallFlags:
 *
 * Flags to set when performing the firmware update or install.
 **/
typedef enum {
	/**
	 * FWUPD_INSTALL_FLAG_NONE:
	 *
	 * No flags set.
	 *
	 * Since: 0.7.0
	 */
	FWUPD_INSTALL_FLAG_NONE = 0,
	/**
	 * FWUPD_INSTALL_FLAG_ALLOW_REINSTALL:
	 *
	 * Allow reinstalling the same version.
	 *
	 * Since: 0.7.0
	 */
	FWUPD_INSTALL_FLAG_ALLOW_REINSTALL = 1 << 1,
	/**
	 * FWUPD_INSTALL_FLAG_ALLOW_OLDER:
	 *
	 * Allow downgrading firmware.
	 *
	 * Since: 0.7.0
	 */
	FWUPD_INSTALL_FLAG_ALLOW_OLDER = 1 << 2,
	/**
	 * FWUPD_INSTALL_FLAG_FORCE:
	 *
	 * Force the update even if not a good idea.
	 *
	 * Since: 0.7.1
	 */
	FWUPD_INSTALL_FLAG_FORCE = 1 << 3,
	/**
	 * FWUPD_INSTALL_FLAG_NO_HISTORY:
	 *
	 * Do not write to the history database.
	 *
	 * Since: 1.0.8
	 */
	FWUPD_INSTALL_FLAG_NO_HISTORY = 1 << 4,
	/**
	 * FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH:
	 *
	 * Allow firmware branch switching.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH = 1 << 5,
	/**
	 * FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM:
	 *
	 * Ignore firmware CRCs and checksums.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM = 1 << 6,
	/**
	 * FWUPD_INSTALL_FLAG_IGNORE_VID_PID:
	 *
	 * Ignore firmware vendor and project checks.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_INSTALL_FLAG_IGNORE_VID_PID = 1 << 7,
	/**
	 * FWUPD_INSTALL_FLAG_NO_SEARCH:
	 *
	 * Do not use heuristics when parsing the image.
	 *
	 * Since: 1.5.0
	 */
	FWUPD_INSTALL_FLAG_NO_SEARCH = 1 << 8,
	/**
	 * FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS:
	 *
	 * Ignore version requirement checks.
	 *
	 * Since: 1.9.21
	 */
	FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS = 1 << 9,
	/*< private >*/
	FWUPD_INSTALL_FLAG_UNKNOWN = G_MAXUINT64,
} FwupdInstallFlags;

/**
 * FwupdSelfSignFlags:
 *
 * Flags to set when performing the firmware update or install.
 **/
typedef enum {
	/**
	 * FWUPD_SELF_SIGN_FLAG_NONE:
	 *
	 * No flags set.
	 *
	 * Since: 1.2.6
	 */
	FWUPD_SELF_SIGN_FLAG_NONE = 0,
	/**
	 * FWUPD_SELF_SIGN_FLAG_ADD_TIMESTAMP:
	 *
	 * Add the timestamp to the detached signature.
	 *
	 * Since: 1.2.6
	 */
	FWUPD_SELF_SIGN_FLAG_ADD_TIMESTAMP = 1 << 0,
	/**
	 * FWUPD_SELF_SIGN_FLAG_ADD_CERT:
	 *
	 * Add the certificate to the detached signature.
	 *
	 * Since: 1.2.6
	 */
	FWUPD_SELF_SIGN_FLAG_ADD_CERT = 1 << 1,
	/*< private >*/
	FWUPD_SELF_SIGN_FLAG_UNKNOWN = G_MAXUINT64,
} FwupdSelfSignFlags;

/**
 * FwupdUpdateState:
 *
 * The update state.
 **/
typedef enum {
	/**
	 * FWUPD_UPDATE_STATE_UNKNOWN:
	 *
	 * Unknown.
	 *
	 * Since: 0.7.0
	 */
	FWUPD_UPDATE_STATE_UNKNOWN,
	/**
	 * FWUPD_UPDATE_STATE_PENDING:
	 *
	 * Update is pending.
	 *
	 * Since: 0.7.0
	 */
	FWUPD_UPDATE_STATE_PENDING,
	/**
	 * FWUPD_UPDATE_STATE_SUCCESS:
	 *
	 * Update was successful.
	 *
	 * Since: 0.7.0
	 */
	FWUPD_UPDATE_STATE_SUCCESS,
	/**
	 * FWUPD_UPDATE_STATE_FAILED:
	 *
	 * Update failed.
	 *
	 * Since: 0.7.0
	 */
	FWUPD_UPDATE_STATE_FAILED,
	/**
	 * FWUPD_UPDATE_STATE_NEEDS_REBOOT:
	 *
	 * Waiting for a reboot to apply.
	 *
	 * Since: 1.0.4
	 */
	FWUPD_UPDATE_STATE_NEEDS_REBOOT,
	/**
	 * FWUPD_UPDATE_STATE_FAILED_TRANSIENT:
	 *
	 * Update failed due to transient issue, e.g. AC power required.
	 *
	 * Since: 1.2.7
	 */
	FWUPD_UPDATE_STATE_FAILED_TRANSIENT,
	/*< private >*/
	FWUPD_UPDATE_STATE_LAST
} FwupdUpdateState;

/**
 * FwupdVersionFormat:
 *
 * The flags used when parsing version numbers.
 *
 * If no verification is required then %FWUPD_VERSION_FORMAT_PLAIN should
 * be used to signify an unparsable text string.
 **/
typedef enum {
	/**
	 * FWUPD_VERSION_FORMAT_UNKNOWN:
	 *
	 * Unknown version format.
	 *
	 * Since: 1.2.9
	 */
	FWUPD_VERSION_FORMAT_UNKNOWN,
	/**
	 * FWUPD_VERSION_FORMAT_PLAIN:
	 *
	 * An unidentified format text string.
	 *
	 * Since: 1.2.9
	 */
	FWUPD_VERSION_FORMAT_PLAIN,
	/**
	 * FWUPD_VERSION_FORMAT_NUMBER:
	 *
	 * A single integer version number.
	 *
	 * Since: 1.2.9
	 */
	FWUPD_VERSION_FORMAT_NUMBER,
	/**
	 * FWUPD_VERSION_FORMAT_PAIR:
	 *
	 * Two AABB.CCDD version numbers.
	 *
	 * Since: 1.2.9
	 */
	FWUPD_VERSION_FORMAT_PAIR,
	/**
	 * FWUPD_VERSION_FORMAT_TRIPLET:
	 *
	 * Microsoft-style AA.BB.CCDD version numbers.
	 *
	 * Since: 1.2.9
	 */
	FWUPD_VERSION_FORMAT_TRIPLET,
	/**
	 * FWUPD_VERSION_FORMAT_QUAD:
	 *
	 * UEFI-style AA.BB.CC.DD version numbers.
	 *
	 * Since: 1.2.9
	 */
	FWUPD_VERSION_FORMAT_QUAD,
	/**
	 * FWUPD_VERSION_FORMAT_BCD:
	 *
	 * Binary coded decimal notation.
	 *
	 * Since: 1.2.9
	 */
	FWUPD_VERSION_FORMAT_BCD,
	/**
	 * FWUPD_VERSION_FORMAT_INTEL_ME:
	 *
	 * Intel ME-style bitshifted notation.
	 *
	 * Since: 1.2.9
	 */
	FWUPD_VERSION_FORMAT_INTEL_ME,
	/**
	 * FWUPD_VERSION_FORMAT_INTEL_ME2:
	 *
	 * Intel ME-style A.B.CC.DDDD notation notation.
	 *
	 * Since: 1.2.9
	 */
	FWUPD_VERSION_FORMAT_INTEL_ME2,
	/**
	 * FWUPD_VERSION_FORMAT_SURFACE_LEGACY:
	 *
	 * Legacy Microsoft Surface 10b.12b.10b.
	 *
	 * Since: 1.3.4
	 */
	FWUPD_VERSION_FORMAT_SURFACE_LEGACY,
	/**
	 * FWUPD_VERSION_FORMAT_SURFACE:
	 *
	 * Microsoft Surface 8b.16b.8b.
	 *
	 * Since: 1.3.4
	 */
	FWUPD_VERSION_FORMAT_SURFACE,
	/**
	 * FWUPD_VERSION_FORMAT_DELL_BIOS:
	 *
	 * Dell BIOS BB.CC.DD style.
	 *
	 * Since: 1.3.6
	 */
	FWUPD_VERSION_FORMAT_DELL_BIOS,
	/**
	 * FWUPD_VERSION_FORMAT_HEX:
	 *
	 * Hexadecimal 0xAABCCDD style.
	 *
	 * Since: 1.4.0
	 */
	FWUPD_VERSION_FORMAT_HEX,
	/**
	 * FWUPD_VERSION_FORMAT_DELL_BIOS_MSB:
	 *
	 * Dell BIOS AA.BB.CC style.
	 *
	 * Since: 1.9.24
	 */
	FWUPD_VERSION_FORMAT_DELL_BIOS_MSB,
	/*< private >*/
	FWUPD_VERSION_FORMAT_LAST
} FwupdVersionFormat;

/**
 * FWUPD_BATTERY_LEVEL_INVALID:
 *
 * This value signifies the battery level is either unset, or the value cannot
 * be discovered.
 */
#define FWUPD_BATTERY_LEVEL_INVALID 101

const gchar *
fwupd_status_to_string(FwupdStatus status);
FwupdStatus
fwupd_status_from_string(const gchar *status);
const gchar *
fwupd_device_flag_to_string(FwupdDeviceFlags device_flag);
FwupdDeviceFlags
fwupd_device_flag_from_string(const gchar *device_flag);
const gchar *
fwupd_device_problem_to_string(FwupdDeviceProblem device_problem);
FwupdDeviceProblem
fwupd_device_problem_from_string(const gchar *device_problem);
const gchar *
fwupd_plugin_flag_to_string(FwupdPluginFlags plugin_flag);
FwupdPluginFlags
fwupd_plugin_flag_from_string(const gchar *plugin_flag);
const gchar *
fwupd_release_flag_to_string(FwupdReleaseFlags release_flag);
FwupdReleaseFlags
fwupd_release_flag_from_string(const gchar *release_flag);
const gchar *
fwupd_release_urgency_to_string(FwupdReleaseUrgency release_urgency);
FwupdReleaseUrgency
fwupd_release_urgency_from_string(const gchar *release_urgency);
const gchar *
fwupd_update_state_to_string(FwupdUpdateState update_state);
FwupdUpdateState
fwupd_update_state_from_string(const gchar *update_state);
const gchar *
fwupd_feature_flag_to_string(FwupdFeatureFlags feature_flag);
FwupdFeatureFlags
fwupd_feature_flag_from_string(const gchar *feature_flag);
FwupdVersionFormat
fwupd_version_format_from_string(const gchar *str);
const gchar *
fwupd_version_format_to_string(FwupdVersionFormat kind);
const gchar *
fwupd_install_flags_to_string(FwupdInstallFlags install_flags);

G_END_DECLS
