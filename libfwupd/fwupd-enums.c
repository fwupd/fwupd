/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-enums.h"

/**
 * fwupd_status_to_string:
 * @status: a status, e.g. %FWUPD_STATUS_DECOMPRESSING
 *
 * Converts an enumerated status to a string.
 *
 * Returns: identifier string
 *
 * Since: 0.1.1
 **/
const gchar *
fwupd_status_to_string(FwupdStatus status)
{
	if (status == FWUPD_STATUS_UNKNOWN)
		return "unknown";
	if (status == FWUPD_STATUS_IDLE)
		return "idle";
	if (status == FWUPD_STATUS_DECOMPRESSING)
		return "decompressing";
	if (status == FWUPD_STATUS_LOADING)
		return "loading";
	if (status == FWUPD_STATUS_DEVICE_RESTART)
		return "device-restart";
	if (status == FWUPD_STATUS_DEVICE_WRITE)
		return "device-write";
	if (status == FWUPD_STATUS_DEVICE_READ)
		return "device-read";
	if (status == FWUPD_STATUS_DEVICE_ERASE)
		return "device-erase";
	if (status == FWUPD_STATUS_DEVICE_VERIFY)
		return "device-verify";
	if (status == FWUPD_STATUS_DEVICE_BUSY)
		return "device-busy";
	if (status == FWUPD_STATUS_SCHEDULING)
		return "scheduling";
	if (status == FWUPD_STATUS_DOWNLOADING)
		return "downloading";
	if (status == FWUPD_STATUS_WAITING_FOR_AUTH)
		return "waiting-for-auth";
	if (status == FWUPD_STATUS_SHUTDOWN)
		return "shutdown";
	if (status == FWUPD_STATUS_WAITING_FOR_USER)
		return "waiting-for-user";
	return NULL;
}

/**
 * fwupd_status_from_string:
 * @status: (nullable): a string, e.g. `decompressing`
 *
 * Converts a string to an enumerated status.
 *
 * Returns: enumerated value
 *
 * Since: 0.1.1
 **/
FwupdStatus
fwupd_status_from_string(const gchar *status)
{
	if (g_strcmp0(status, "unknown") == 0)
		return FWUPD_STATUS_UNKNOWN;
	if (g_strcmp0(status, "idle") == 0)
		return FWUPD_STATUS_IDLE;
	if (g_strcmp0(status, "decompressing") == 0)
		return FWUPD_STATUS_DECOMPRESSING;
	if (g_strcmp0(status, "loading") == 0)
		return FWUPD_STATUS_LOADING;
	if (g_strcmp0(status, "device-restart") == 0)
		return FWUPD_STATUS_DEVICE_RESTART;
	if (g_strcmp0(status, "device-write") == 0)
		return FWUPD_STATUS_DEVICE_WRITE;
	if (g_strcmp0(status, "device-verify") == 0)
		return FWUPD_STATUS_DEVICE_VERIFY;
	if (g_strcmp0(status, "scheduling") == 0)
		return FWUPD_STATUS_SCHEDULING;
	if (g_strcmp0(status, "downloading") == 0)
		return FWUPD_STATUS_DOWNLOADING;
	if (g_strcmp0(status, "device-read") == 0)
		return FWUPD_STATUS_DEVICE_READ;
	if (g_strcmp0(status, "device-erase") == 0)
		return FWUPD_STATUS_DEVICE_ERASE;
	if (g_strcmp0(status, "device-busy") == 0)
		return FWUPD_STATUS_DEVICE_BUSY;
	if (g_strcmp0(status, "waiting-for-auth") == 0)
		return FWUPD_STATUS_WAITING_FOR_AUTH;
	if (g_strcmp0(status, "shutdown") == 0)
		return FWUPD_STATUS_SHUTDOWN;
	if (g_strcmp0(status, "waiting-for-user") == 0)
		return FWUPD_STATUS_WAITING_FOR_USER;
	return FWUPD_STATUS_LAST;
}

/**
 * fwupd_device_flag_to_string:
 * @device_flag: a device flag, e.g. %FWUPD_DEVICE_FLAG_REQUIRE_AC
 *
 * Converts a device flag to a string.
 *
 * Returns: identifier string
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_device_flag_to_string(FwupdDeviceFlags device_flag)
{
	if (device_flag == FWUPD_DEVICE_FLAG_NONE)
		return "none";
	if (device_flag == FWUPD_DEVICE_FLAG_INTERNAL)
		return "internal";
	if (device_flag == FWUPD_DEVICE_FLAG_UPDATABLE)
		return "updatable";
	if (device_flag == FWUPD_DEVICE_FLAG_REQUIRE_AC)
		return "require-ac";
	if (device_flag == FWUPD_DEVICE_FLAG_LOCKED)
		return "locked";
	if (device_flag == FWUPD_DEVICE_FLAG_SUPPORTED)
		return "supported";
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER)
		return "needs-bootloader";
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_REBOOT)
		return "needs-reboot";
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN)
		return "needs-shutdown";
	if (device_flag == FWUPD_DEVICE_FLAG_REPORTED)
		return "reported";
	if (device_flag == FWUPD_DEVICE_FLAG_NOTIFIED)
		return "notified";
	if (device_flag == FWUPD_DEVICE_FLAG_IS_BOOTLOADER)
		return "is-bootloader";
	if (device_flag == FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG)
		return "wait-for-replug";
	if (device_flag == FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED)
		return "another-write-required";
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)
		return "needs-activation";
	if (device_flag == FWUPD_DEVICE_FLAG_HISTORICAL)
		return "historical";
	if (device_flag == FWUPD_DEVICE_FLAG_WILL_DISAPPEAR)
		return "will-disappear";
	if (device_flag == FWUPD_DEVICE_FLAG_CAN_VERIFY)
		return "can-verify";
	if (device_flag == FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE)
		return "can-verify-image";
	if (device_flag == FWUPD_DEVICE_FLAG_DUAL_IMAGE)
		return "dual-image";
	if (device_flag == FWUPD_DEVICE_FLAG_SELF_RECOVERY)
		return "self-recovery";
	if (device_flag == FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE)
		return "usable-during-update";
	if (device_flag == FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED)
		return "version-check-required";
	if (device_flag == FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES)
		return "install-all-releases";
	if (device_flag == FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN)
		return "updatable-hidden";
	if (device_flag == FWUPD_DEVICE_FLAG_HAS_MULTIPLE_BRANCHES)
		return "has-multiple-branches";
	if (device_flag == FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL)
		return "backup-before-install";
	if (device_flag == FWUPD_DEVICE_FLAG_WILDCARD_INSTALL)
		return "wildcard-install";
	if (device_flag == FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE)
		return "only-version-upgrade";
	if (device_flag == FWUPD_DEVICE_FLAG_UNREACHABLE)
		return "unreachable";
	if (device_flag == FWUPD_DEVICE_FLAG_AFFECTS_FDE)
		return "affects-fde";
	if (device_flag == FWUPD_DEVICE_FLAG_END_OF_LIFE)
		return "end-of-life";
	if (device_flag == FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD)
		return "signed-payload";
	if (device_flag == FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD)
		return "unsigned-payload";
	if (device_flag == FWUPD_DEVICE_FLAG_EMULATED)
		return "emulated";
	if (device_flag == FWUPD_DEVICE_FLAG_EMULATION_TAG)
		return "emulation-tag";
	if (device_flag == FWUPD_DEVICE_FLAG_ONLY_EXPLICIT_UPDATES)
		return "only-explicit-updates";
	if (device_flag == FWUPD_DEVICE_FLAG_UNKNOWN)
		return "unknown";
	return NULL;
}

/**
 * fwupd_device_flag_from_string:
 * @device_flag: (nullable): a string, e.g. `require-ac`
 *
 * Converts a string to an enumerated device flag.
 *
 * Returns: enumerated value
 *
 * Since: 0.7.0
 **/
FwupdDeviceFlags
fwupd_device_flag_from_string(const gchar *device_flag)
{
	if (g_strcmp0(device_flag, "none") == 0)
		return FWUPD_DEVICE_FLAG_NONE;
	if (g_strcmp0(device_flag, "internal") == 0)
		return FWUPD_DEVICE_FLAG_INTERNAL;
	if (g_strcmp0(device_flag, "updatable") == 0 || g_strcmp0(device_flag, "allow-online") == 0)
		return FWUPD_DEVICE_FLAG_UPDATABLE;
	if (g_strcmp0(device_flag, "require-ac") == 0)
		return FWUPD_DEVICE_FLAG_REQUIRE_AC;
	if (g_strcmp0(device_flag, "locked") == 0)
		return FWUPD_DEVICE_FLAG_LOCKED;
	if (g_strcmp0(device_flag, "supported") == 0)
		return FWUPD_DEVICE_FLAG_SUPPORTED;
	if (g_strcmp0(device_flag, "needs-bootloader") == 0)
		return FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER;
	if (g_strcmp0(device_flag, "needs-reboot") == 0)
		return FWUPD_DEVICE_FLAG_NEEDS_REBOOT;
	if (g_strcmp0(device_flag, "needs-shutdown") == 0)
		return FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN;
	if (g_strcmp0(device_flag, "reported") == 0)
		return FWUPD_DEVICE_FLAG_REPORTED;
	if (g_strcmp0(device_flag, "notified") == 0)
		return FWUPD_DEVICE_FLAG_NOTIFIED;
	if (g_strcmp0(device_flag, "is-bootloader") == 0)
		return FWUPD_DEVICE_FLAG_IS_BOOTLOADER;
	if (g_strcmp0(device_flag, "wait-for-replug") == 0)
		return FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG;
	if (g_strcmp0(device_flag, "another-write-required") == 0)
		return FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED;
	if (g_strcmp0(device_flag, "needs-activation") == 0)
		return FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION;
	if (g_strcmp0(device_flag, "historical") == 0)
		return FWUPD_DEVICE_FLAG_HISTORICAL;
	if (g_strcmp0(device_flag, "will-disappear") == 0)
		return FWUPD_DEVICE_FLAG_WILL_DISAPPEAR;
	if (g_strcmp0(device_flag, "can-verify") == 0)
		return FWUPD_DEVICE_FLAG_CAN_VERIFY;
	if (g_strcmp0(device_flag, "can-verify-image") == 0)
		return FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE;
	if (g_strcmp0(device_flag, "dual-image") == 0)
		return FWUPD_DEVICE_FLAG_DUAL_IMAGE;
	if (g_strcmp0(device_flag, "self-recovery") == 0)
		return FWUPD_DEVICE_FLAG_SELF_RECOVERY;
	if (g_strcmp0(device_flag, "usable-during-update") == 0)
		return FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE;
	if (g_strcmp0(device_flag, "version-check-required") == 0)
		return FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED;
	if (g_strcmp0(device_flag, "install-all-releases") == 0)
		return FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES;
	if (g_strcmp0(device_flag, "updatable-hidden") == 0)
		return FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN;
	if (g_strcmp0(device_flag, "has-multiple-branches") == 0)
		return FWUPD_DEVICE_FLAG_HAS_MULTIPLE_BRANCHES;
	if (g_strcmp0(device_flag, "backup-before-install") == 0)
		return FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL;
	if (g_strcmp0(device_flag, "wildcard-install") == 0)
		return FWUPD_DEVICE_FLAG_WILDCARD_INSTALL;
	if (g_strcmp0(device_flag, "only-version-upgrade") == 0)
		return FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE;
	if (g_strcmp0(device_flag, "unreachable") == 0)
		return FWUPD_DEVICE_FLAG_UNREACHABLE;
	if (g_strcmp0(device_flag, "affects-fde") == 0)
		return FWUPD_DEVICE_FLAG_AFFECTS_FDE;
	if (g_strcmp0(device_flag, "end-of-life") == 0)
		return FWUPD_DEVICE_FLAG_END_OF_LIFE;
	if (g_strcmp0(device_flag, "signed-payload") == 0)
		return FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD;
	if (g_strcmp0(device_flag, "unsigned-payload") == 0)
		return FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD;
	if (g_strcmp0(device_flag, "emulated") == 0)
		return FWUPD_DEVICE_FLAG_EMULATED;
	if (g_strcmp0(device_flag, "emulation-tag") == 0)
		return FWUPD_DEVICE_FLAG_EMULATION_TAG;
	if (g_strcmp0(device_flag, "only-explicit-updates") == 0)
		return FWUPD_DEVICE_FLAG_ONLY_EXPLICIT_UPDATES;
	return FWUPD_DEVICE_FLAG_UNKNOWN;
}

/**
 * fwupd_device_problem_to_string:
 * @device_problem: a device inhibit kind, e.g. %FWUPD_DEVICE_PROBLEM_SYSTEM_POWER_TOO_LOW
 *
 * Converts a device inhibit kind to a string.
 *
 * Returns: identifier string
 *
 * Since: 1.8.1
 **/
const gchar *
fwupd_device_problem_to_string(FwupdDeviceProblem device_problem)
{
	if (device_problem == FWUPD_DEVICE_PROBLEM_NONE)
		return "none";
	if (device_problem == FWUPD_DEVICE_PROBLEM_SYSTEM_POWER_TOO_LOW)
		return "system-power-too-low";
	if (device_problem == FWUPD_DEVICE_PROBLEM_UNREACHABLE)
		return "unreachable";
	if (device_problem == FWUPD_DEVICE_PROBLEM_POWER_TOO_LOW)
		return "power-too-low";
	if (device_problem == FWUPD_DEVICE_PROBLEM_UPDATE_PENDING)
		return "update-pending";
	if (device_problem == FWUPD_DEVICE_PROBLEM_REQUIRE_AC_POWER)
		return "require-ac-power";
	if (device_problem == FWUPD_DEVICE_PROBLEM_LID_IS_CLOSED)
		return "lid-is-closed";
	if (device_problem == FWUPD_DEVICE_PROBLEM_IS_EMULATED)
		return "is-emulated";
	if (device_problem == FWUPD_DEVICE_PROBLEM_MISSING_LICENSE)
		return "missing-license";
	if (device_problem == FWUPD_DEVICE_PROBLEM_SYSTEM_INHIBIT)
		return "system-inhibit";
	if (device_problem == FWUPD_DEVICE_PROBLEM_UPDATE_IN_PROGRESS)
		return "update-in-progress";
	if (device_problem == FWUPD_DEVICE_PROBLEM_IN_USE)
		return "in-use";
	if (device_problem == FWUPD_DEVICE_PROBLEM_DISPLAY_REQUIRED)
		return "display-required";
	if (device_problem == FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY)
		return "lower-priority";
	if (device_problem == FWUPD_DEVICE_PROBLEM_UNKNOWN)
		return "unknown";
	return NULL;
}

/**
 * fwupd_device_problem_from_string:
 * @device_problem: (nullable): a string, e.g. `require-ac`
 *
 * Converts a string to a enumerated device inhibit kind.
 *
 * Returns: enumerated value
 *
 * Since: 1.8.1
 **/
FwupdDeviceProblem
fwupd_device_problem_from_string(const gchar *device_problem)
{
	if (g_strcmp0(device_problem, "none") == 0)
		return FWUPD_DEVICE_PROBLEM_NONE;
	if (g_strcmp0(device_problem, "system-power-too-low") == 0)
		return FWUPD_DEVICE_PROBLEM_SYSTEM_POWER_TOO_LOW;
	if (g_strcmp0(device_problem, "unreachable") == 0)
		return FWUPD_DEVICE_PROBLEM_UNREACHABLE;
	if (g_strcmp0(device_problem, "power-too-low") == 0)
		return FWUPD_DEVICE_PROBLEM_POWER_TOO_LOW;
	if (g_strcmp0(device_problem, "update-pending") == 0)
		return FWUPD_DEVICE_PROBLEM_UPDATE_PENDING;
	if (g_strcmp0(device_problem, "require-ac-power") == 0)
		return FWUPD_DEVICE_PROBLEM_REQUIRE_AC_POWER;
	if (g_strcmp0(device_problem, "lid-is-closed") == 0)
		return FWUPD_DEVICE_PROBLEM_LID_IS_CLOSED;
	if (g_strcmp0(device_problem, "is-emulated") == 0)
		return FWUPD_DEVICE_PROBLEM_IS_EMULATED;
	if (g_strcmp0(device_problem, "missing-license") == 0)
		return FWUPD_DEVICE_PROBLEM_MISSING_LICENSE;
	if (g_strcmp0(device_problem, "system-inhibit") == 0)
		return FWUPD_DEVICE_PROBLEM_SYSTEM_INHIBIT;
	if (g_strcmp0(device_problem, "update-in-progress") == 0)
		return FWUPD_DEVICE_PROBLEM_UPDATE_IN_PROGRESS;
	if (g_strcmp0(device_problem, "in-use") == 0)
		return FWUPD_DEVICE_PROBLEM_IN_USE;
	if (g_strcmp0(device_problem, "display-required") == 0)
		return FWUPD_DEVICE_PROBLEM_DISPLAY_REQUIRED;
	if (g_strcmp0(device_problem, "lower-priority") == 0)
		return FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY;
	return FWUPD_DEVICE_PROBLEM_UNKNOWN;
}

/**
 * fwupd_plugin_flag_to_string:
 * @plugin_flag: plugin flags, e.g. %FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE
 *
 * Converts an enumerated plugin flag to a string.
 *
 * Returns: identifier string
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_plugin_flag_to_string(FwupdPluginFlags plugin_flag)
{
	if (plugin_flag == FWUPD_PLUGIN_FLAG_NONE)
		return "none";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_DISABLED)
		return "disabled";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_USER_WARNING)
		return "user-warning";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE)
		return "clear-updatable";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_NO_HARDWARE)
		return "no-hardware";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED)
		return "capsules-unsupported";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_UNLOCK_REQUIRED)
		return "unlock-required";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_EFIVAR_NOT_MOUNTED)
		return "efivar-not-mounted";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND)
		return "esp-not-found";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_ESP_NOT_VALID)
		return "esp-not-valid";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_LEGACY_BIOS)
		return "legacy-bios";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_FAILED_OPEN)
		return "failed-open";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_REQUIRE_HWID)
		return "require-hwid";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_KERNEL_TOO_OLD)
		return "kernel-too-old";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_UNKNOWN)
		return "unknown";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_AUTH_REQUIRED)
		return "auth-required";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_SECURE_CONFIG)
		return "secure-config";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_MODULAR)
		return "modular";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_MEASURE_SYSTEM_INTEGRITY)
		return "measure-system-integrity";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_READY)
		return "ready";
	if (plugin_flag == FWUPD_PLUGIN_FLAG_TEST_ONLY)
		return "test-only";
	return NULL;
}

/**
 * fwupd_plugin_flag_from_string:
 * @plugin_flag: (nullable): a string, e.g. `require-ac`
 *
 * Converts a string to an enumerated plugin flag.
 *
 * Returns: enumerated value
 *
 * Since: 1.5.0
 **/
FwupdPluginFlags
fwupd_plugin_flag_from_string(const gchar *plugin_flag)
{
	if (g_strcmp0(plugin_flag, "none") == 0)
		return FWUPD_PLUGIN_FLAG_NONE;
	if (g_strcmp0(plugin_flag, "disabled") == 0)
		return FWUPD_PLUGIN_FLAG_DISABLED;
	if (g_strcmp0(plugin_flag, "user-warning") == 0)
		return FWUPD_PLUGIN_FLAG_USER_WARNING;
	if (g_strcmp0(plugin_flag, "clear-updatable") == 0)
		return FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE;
	if (g_strcmp0(plugin_flag, "no-hardware") == 0)
		return FWUPD_PLUGIN_FLAG_NO_HARDWARE;
	if (g_strcmp0(plugin_flag, "capsules-unsupported") == 0)
		return FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED;
	if (g_strcmp0(plugin_flag, "unlock-required") == 0)
		return FWUPD_PLUGIN_FLAG_UNLOCK_REQUIRED;
	if (g_strcmp0(plugin_flag, "efivar-not-mounted") == 0)
		return FWUPD_PLUGIN_FLAG_EFIVAR_NOT_MOUNTED;
	if (g_strcmp0(plugin_flag, "esp-not-found") == 0)
		return FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND;
	if (g_strcmp0(plugin_flag, "esp-not-valid") == 0)
		return FWUPD_PLUGIN_FLAG_ESP_NOT_VALID;
	if (g_strcmp0(plugin_flag, "legacy-bios") == 0)
		return FWUPD_PLUGIN_FLAG_LEGACY_BIOS;
	if (g_strcmp0(plugin_flag, "failed-open") == 0)
		return FWUPD_PLUGIN_FLAG_FAILED_OPEN;
	if (g_strcmp0(plugin_flag, "require-hwid") == 0)
		return FWUPD_PLUGIN_FLAG_REQUIRE_HWID;
	if (g_strcmp0(plugin_flag, "kernel-too-old") == 0)
		return FWUPD_PLUGIN_FLAG_KERNEL_TOO_OLD;
	if (g_strcmp0(plugin_flag, "auth-required") == 0)
		return FWUPD_PLUGIN_FLAG_AUTH_REQUIRED;
	if (g_strcmp0(plugin_flag, "secure-config") == 0)
		return FWUPD_PLUGIN_FLAG_SECURE_CONFIG;
	if (g_strcmp0(plugin_flag, "modular") == 0)
		return FWUPD_PLUGIN_FLAG_MODULAR;
	if (g_strcmp0(plugin_flag, "measure-system-integrity") == 0)
		return FWUPD_PLUGIN_FLAG_MEASURE_SYSTEM_INTEGRITY;
	if (g_strcmp0(plugin_flag, "ready") == 0)
		return FWUPD_PLUGIN_FLAG_READY;
	if (g_strcmp0(plugin_flag, "test-only") == 0)
		return FWUPD_PLUGIN_FLAG_TEST_ONLY;
	return FWUPD_PLUGIN_FLAG_UNKNOWN;
}

/**
 * fwupd_update_state_to_string:
 * @update_state: the update state, e.g. %FWUPD_UPDATE_STATE_PENDING
 *
 * Converts an enumerated update state to a string.
 *
 * Returns: identifier string
 *
 * Since: 0.7.0
 **/
const gchar *
fwupd_update_state_to_string(FwupdUpdateState update_state)
{
	if (update_state == FWUPD_UPDATE_STATE_UNKNOWN)
		return "unknown";
	if (update_state == FWUPD_UPDATE_STATE_PENDING)
		return "pending";
	if (update_state == FWUPD_UPDATE_STATE_SUCCESS)
		return "success";
	if (update_state == FWUPD_UPDATE_STATE_FAILED)
		return "failed";
	if (update_state == FWUPD_UPDATE_STATE_FAILED_TRANSIENT)
		return "failed-transient";
	if (update_state == FWUPD_UPDATE_STATE_NEEDS_REBOOT)
		return "needs-reboot";
	return NULL;
}

/**
 * fwupd_update_state_from_string:
 * @update_state: (nullable): a string, e.g. `pending`
 *
 * Converts a string to an enumerated update state.
 *
 * Returns: enumerated value
 *
 * Since: 0.7.0
 **/
FwupdUpdateState
fwupd_update_state_from_string(const gchar *update_state)
{
	if (g_strcmp0(update_state, "unknown") == 0)
		return FWUPD_UPDATE_STATE_UNKNOWN;
	if (g_strcmp0(update_state, "pending") == 0)
		return FWUPD_UPDATE_STATE_PENDING;
	if (g_strcmp0(update_state, "success") == 0)
		return FWUPD_UPDATE_STATE_SUCCESS;
	if (g_strcmp0(update_state, "failed") == 0)
		return FWUPD_UPDATE_STATE_FAILED;
	if (g_strcmp0(update_state, "failed-transient") == 0)
		return FWUPD_UPDATE_STATE_FAILED_TRANSIENT;
	if (g_strcmp0(update_state, "needs-reboot") == 0)
		return FWUPD_UPDATE_STATE_NEEDS_REBOOT;
	return FWUPD_UPDATE_STATE_UNKNOWN;
}

/**
 * fwupd_feature_flag_to_string:
 * @feature_flag: a single feature flag, e.g. %FWUPD_FEATURE_FLAG_DETACH_ACTION
 *
 * Converts a feature flag to a string.
 *
 * Returns: identifier string
 *
 * Since: 1.4.5
 **/
const gchar *
fwupd_feature_flag_to_string(FwupdFeatureFlags feature_flag)
{
	if (feature_flag == FWUPD_FEATURE_FLAG_NONE)
		return "none";
	if (feature_flag == FWUPD_FEATURE_FLAG_CAN_REPORT)
		return "can-report";
	if (feature_flag == FWUPD_FEATURE_FLAG_DETACH_ACTION)
		return "detach-action";
	if (feature_flag == FWUPD_FEATURE_FLAG_UPDATE_ACTION)
		return "update-action";
	if (feature_flag == FWUPD_FEATURE_FLAG_SWITCH_BRANCH)
		return "switch-branch";
	if (feature_flag == FWUPD_FEATURE_FLAG_REQUESTS)
		return "requests";
	if (feature_flag == FWUPD_FEATURE_FLAG_FDE_WARNING)
		return "fde-warning";
	if (feature_flag == FWUPD_FEATURE_FLAG_COMMUNITY_TEXT)
		return "community-text";
	if (feature_flag == FWUPD_FEATURE_FLAG_SHOW_PROBLEMS)
		return "show-problems";
	if (feature_flag == FWUPD_FEATURE_FLAG_ALLOW_AUTHENTICATION)
		return "allow-authentication";
	if (feature_flag == FWUPD_FEATURE_FLAG_REQUESTS_NON_GENERIC)
		return "requests-non-generic";
	return NULL;
}

/**
 * fwupd_feature_flag_from_string:
 * @feature_flag: (nullable): a string, e.g. `detach-action`
 *
 * Converts a string to an enumerated feature flag.
 *
 * Returns: enumerated value
 *
 * Since: 1.4.5
 **/
FwupdFeatureFlags
fwupd_feature_flag_from_string(const gchar *feature_flag)
{
	if (g_strcmp0(feature_flag, "none") == 0)
		return FWUPD_FEATURE_FLAG_NONE;
	if (g_strcmp0(feature_flag, "can-report") == 0)
		return FWUPD_FEATURE_FLAG_CAN_REPORT;
	if (g_strcmp0(feature_flag, "detach-action") == 0)
		return FWUPD_FEATURE_FLAG_DETACH_ACTION;
	if (g_strcmp0(feature_flag, "update-action") == 0)
		return FWUPD_FEATURE_FLAG_UPDATE_ACTION;
	if (g_strcmp0(feature_flag, "switch-branch") == 0)
		return FWUPD_FEATURE_FLAG_SWITCH_BRANCH;
	if (g_strcmp0(feature_flag, "requests") == 0)
		return FWUPD_FEATURE_FLAG_REQUESTS;
	if (g_strcmp0(feature_flag, "fde-warning") == 0)
		return FWUPD_FEATURE_FLAG_FDE_WARNING;
	if (g_strcmp0(feature_flag, "community-text") == 0)
		return FWUPD_FEATURE_FLAG_COMMUNITY_TEXT;
	if (g_strcmp0(feature_flag, "show-problems") == 0)
		return FWUPD_FEATURE_FLAG_SHOW_PROBLEMS;
	if (g_strcmp0(feature_flag, "allow-authentication") == 0)
		return FWUPD_FEATURE_FLAG_ALLOW_AUTHENTICATION;
	if (g_strcmp0(feature_flag, "requests-non-generic") == 0)
		return FWUPD_FEATURE_FLAG_REQUESTS_NON_GENERIC;
	return FWUPD_FEATURE_FLAG_UNKNOWN;
}

/**
 * fwupd_release_flag_to_string:
 * @release_flag: a release flag, e.g. %FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD
 *
 * Converts an enumerated release flag to a string.
 *
 * Returns: identifier string
 *
 * Since: 1.2.6
 **/
const gchar *
fwupd_release_flag_to_string(FwupdReleaseFlags release_flag)
{
	if (release_flag == FWUPD_RELEASE_FLAG_NONE)
		return "none";
	if (release_flag == FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD)
		return "trusted-payload";
	if (release_flag == FWUPD_RELEASE_FLAG_TRUSTED_METADATA)
		return "trusted-metadata";
	if (release_flag == FWUPD_RELEASE_FLAG_IS_UPGRADE)
		return "is-upgrade";
	if (release_flag == FWUPD_RELEASE_FLAG_IS_DOWNGRADE)
		return "is-downgrade";
	if (release_flag == FWUPD_RELEASE_FLAG_BLOCKED_VERSION)
		return "blocked-version";
	if (release_flag == FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL)
		return "blocked-approval";
	if (release_flag == FWUPD_RELEASE_FLAG_IS_ALTERNATE_BRANCH)
		return "is-alternate-branch";
	if (release_flag == FWUPD_RELEASE_FLAG_IS_COMMUNITY)
		return "is-community";
	if (release_flag == FWUPD_RELEASE_FLAG_TRUSTED_REPORT)
		return "trusted-report";
	return NULL;
}

/**
 * fwupd_release_flag_from_string:
 * @release_flag: (nullable): a string, e.g. `trusted-payload`
 *
 * Converts a string to an enumerated release flag.
 *
 * Returns: enumerated value
 *
 * Since: 1.2.6
 **/
FwupdReleaseFlags
fwupd_release_flag_from_string(const gchar *release_flag)
{
	if (g_strcmp0(release_flag, "trusted-payload") == 0)
		return FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD;
	if (g_strcmp0(release_flag, "trusted-metadata") == 0)
		return FWUPD_RELEASE_FLAG_TRUSTED_METADATA;
	if (g_strcmp0(release_flag, "is-upgrade") == 0)
		return FWUPD_RELEASE_FLAG_IS_UPGRADE;
	if (g_strcmp0(release_flag, "is-downgrade") == 0)
		return FWUPD_RELEASE_FLAG_IS_DOWNGRADE;
	if (g_strcmp0(release_flag, "blocked-version") == 0)
		return FWUPD_RELEASE_FLAG_BLOCKED_VERSION;
	if (g_strcmp0(release_flag, "blocked-approval") == 0)
		return FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL;
	if (g_strcmp0(release_flag, "is-alternate-branch") == 0)
		return FWUPD_RELEASE_FLAG_IS_ALTERNATE_BRANCH;
	if (g_strcmp0(release_flag, "is-community") == 0)
		return FWUPD_RELEASE_FLAG_IS_COMMUNITY;
	if (g_strcmp0(release_flag, "trusted-report") == 0)
		return FWUPD_RELEASE_FLAG_TRUSTED_REPORT;
	return FWUPD_RELEASE_FLAG_NONE;
}

/**
 * fwupd_release_urgency_to_string:
 * @release_urgency: a release urgency, e.g. %FWUPD_RELEASE_URGENCY_HIGH
 *
 * Converts an enumerated release urgency to a string.
 *
 * Returns: identifier string
 *
 * Since: 1.4.0
 **/
const gchar *
fwupd_release_urgency_to_string(FwupdReleaseUrgency release_urgency)
{
	if (release_urgency == FWUPD_RELEASE_URGENCY_LOW)
		return "low";
	if (release_urgency == FWUPD_RELEASE_URGENCY_MEDIUM)
		return "medium";
	if (release_urgency == FWUPD_RELEASE_URGENCY_HIGH)
		return "high";
	if (release_urgency == FWUPD_RELEASE_URGENCY_CRITICAL)
		return "critical";
	return NULL;
}

/**
 * fwupd_release_urgency_from_string:
 * @release_urgency: (nullable): a string, e.g. `low`
 *
 * Converts a string to an enumerated release urgency value.
 *
 * Returns: enumerated value
 *
 * Since: 1.4.0
 **/
FwupdReleaseUrgency
fwupd_release_urgency_from_string(const gchar *release_urgency)
{
	if (g_strcmp0(release_urgency, "low") == 0)
		return FWUPD_RELEASE_URGENCY_LOW;
	if (g_strcmp0(release_urgency, "medium") == 0)
		return FWUPD_RELEASE_URGENCY_MEDIUM;
	if (g_strcmp0(release_urgency, "high") == 0)
		return FWUPD_RELEASE_URGENCY_HIGH;
	if (g_strcmp0(release_urgency, "critical") == 0)
		return FWUPD_RELEASE_URGENCY_CRITICAL;
	return FWUPD_RELEASE_URGENCY_UNKNOWN;
}

/**
 * fwupd_version_format_from_string:
 * @str: (nullable): a string, e.g. `quad`
 *
 * Converts text to a display version type.
 *
 * Returns: an enumerated version format, e.g. %FWUPD_VERSION_FORMAT_TRIPLET
 *
 * Since: 1.2.9
 **/
FwupdVersionFormat
fwupd_version_format_from_string(const gchar *str)
{
	if (g_strcmp0(str, "plain") == 0)
		return FWUPD_VERSION_FORMAT_PLAIN;
	if (g_strcmp0(str, "pair") == 0)
		return FWUPD_VERSION_FORMAT_PAIR;
	if (g_strcmp0(str, "number") == 0)
		return FWUPD_VERSION_FORMAT_NUMBER;
	if (g_strcmp0(str, "triplet") == 0)
		return FWUPD_VERSION_FORMAT_TRIPLET;
	if (g_strcmp0(str, "quad") == 0)
		return FWUPD_VERSION_FORMAT_QUAD;
	if (g_strcmp0(str, "bcd") == 0)
		return FWUPD_VERSION_FORMAT_BCD;
	if (g_strcmp0(str, "intel-me") == 0)
		return FWUPD_VERSION_FORMAT_INTEL_ME;
	if (g_strcmp0(str, "intel-me2") == 0)
		return FWUPD_VERSION_FORMAT_INTEL_ME2;
	if (g_strcmp0(str, "surface-legacy") == 0)
		return FWUPD_VERSION_FORMAT_SURFACE_LEGACY;
	if (g_strcmp0(str, "surface") == 0)
		return FWUPD_VERSION_FORMAT_SURFACE;
	if (g_strcmp0(str, "dell-bios") == 0)
		return FWUPD_VERSION_FORMAT_DELL_BIOS;
	if (g_strcmp0(str, "hex") == 0)
		return FWUPD_VERSION_FORMAT_HEX;
	if (g_strcmp0(str, "dell-bios-msb") == 0)
		return FWUPD_VERSION_FORMAT_DELL_BIOS_MSB;
	return FWUPD_VERSION_FORMAT_UNKNOWN;
}

/**
 * fwupd_version_format_to_string:
 * @kind: a version format, e.g. %FWUPD_VERSION_FORMAT_TRIPLET
 *
 * Converts an enumerated version format to text.
 *
 * Returns: a string, e.g. `quad`, or %NULL if not known
 *
 * Since: 1.2.9
 **/
const gchar *
fwupd_version_format_to_string(FwupdVersionFormat kind)
{
	if (kind == FWUPD_VERSION_FORMAT_PLAIN)
		return "plain";
	if (kind == FWUPD_VERSION_FORMAT_NUMBER)
		return "number";
	if (kind == FWUPD_VERSION_FORMAT_PAIR)
		return "pair";
	if (kind == FWUPD_VERSION_FORMAT_TRIPLET)
		return "triplet";
	if (kind == FWUPD_VERSION_FORMAT_QUAD)
		return "quad";
	if (kind == FWUPD_VERSION_FORMAT_BCD)
		return "bcd";
	if (kind == FWUPD_VERSION_FORMAT_INTEL_ME)
		return "intel-me";
	if (kind == FWUPD_VERSION_FORMAT_INTEL_ME2)
		return "intel-me2";
	if (kind == FWUPD_VERSION_FORMAT_SURFACE_LEGACY)
		return "surface-legacy";
	if (kind == FWUPD_VERSION_FORMAT_SURFACE)
		return "surface";
	if (kind == FWUPD_VERSION_FORMAT_DELL_BIOS)
		return "dell-bios";
	if (kind == FWUPD_VERSION_FORMAT_HEX)
		return "hex";
	if (kind == FWUPD_VERSION_FORMAT_DELL_BIOS_MSB)
		return "dell-bios-msb";
	return NULL;
}

/**
 * fwupd_install_flags_to_string:
 * @install_flags: a #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 *
 * Converts an install flag to text.
 *
 * Returns: a string, e.g. `force`, or %NULL if not known
 *
 * Since: 2.0.0
 **/
const gchar *
fwupd_install_flags_to_string(FwupdInstallFlags install_flags)
{
	if (install_flags == FWUPD_INSTALL_FLAG_NONE)
		return "none";
	if (install_flags == FWUPD_INSTALL_FLAG_ALLOW_REINSTALL)
		return "allow-reinstall";
	if (install_flags == FWUPD_INSTALL_FLAG_ALLOW_OLDER)
		return "allow-older";
	if (install_flags == FWUPD_INSTALL_FLAG_FORCE)
		return "force";
	if (install_flags == FWUPD_INSTALL_FLAG_NO_HISTORY)
		return "no-history";
	if (install_flags == FWUPD_INSTALL_FLAG_ALLOW_BRANCH_SWITCH)
		return "allow-branch-switch";
	if (install_flags == FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM)
		return "ignore-checksum";
	if (install_flags == FWUPD_INSTALL_FLAG_IGNORE_VID_PID)
		return "ignore-vid-pid";
	if (install_flags == FWUPD_INSTALL_FLAG_NO_SEARCH)
		return "no-search";
	if (install_flags == FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS)
		return "ignore-requirements";
	return NULL;
}
