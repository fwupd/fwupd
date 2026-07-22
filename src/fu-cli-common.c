/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <curl/curl.h>
#include <glib/gi18n.h>
#include <unistd.h>
#include <xmlb.h>

#include "fu-cli-common.h"
#include "fu-console.h"

static gboolean
fu_cli_is_interesting_child(GPtrArray *devs, FwupdDevice *dev)
{
	for (guint i = 0; i < devs->len; i++) {
		FwupdDevice *child = g_ptr_array_index(devs, i);
		if (fwupd_device_get_parent(child) != dev)
			continue;
		if (fu_cli_is_interesting_device(devs, child))
			return TRUE;
	}
	return FALSE;
}

gboolean
fu_cli_is_interesting_device(GPtrArray *devs, FwupdDevice *dev)
{
	if (fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE))
		return TRUE;
	if (fwupd_device_get_update_error(dev) != NULL)
		return TRUE;
	if (fwupd_device_get_version(dev) != NULL)
		return TRUE;
	/* device not plugged in, get-details */
	if (fwupd_device_get_flags(dev) == 0)
		return TRUE;
	if (fu_cli_is_interesting_child(devs, dev))
		return TRUE;
	return FALSE;
}

gchar *
fu_cli_get_user_cache_path(const gchar *fn)
{
	const gchar *root = g_get_user_cache_dir();
	g_autofree gchar *basename = g_path_get_basename(fn);
	g_autofree gchar *cachedir_legacy = NULL;

	/* if run from a systemd unit, use the cache directory set there */
	if (g_getenv("CACHE_DIRECTORY") != NULL)
		root = g_getenv("CACHE_DIRECTORY");

	/* return the legacy path if it exists rather than renaming it to
	 * prevent problems when using old and new versions of fwupd */
	cachedir_legacy = g_build_filename(root, "fwupdmgr", NULL);
	if (g_file_test(cachedir_legacy, G_FILE_TEST_IS_DIR))
		return g_build_filename(cachedir_legacy, basename, NULL);

	return g_build_filename(root, "fwupd", basename, NULL);
}

const gchar *
fu_cli_request_flag_to_string(FwupdRequestFlags request_flag)
{
	if (request_flag == FWUPD_REQUEST_FLAG_NONE)
		return NULL;
	if (request_flag == FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE) {
		/* TRANSLATORS: ask the user to do a simple task which should be translated */
		return _("Message");
	}
	if (request_flag == FWUPD_REQUEST_FLAG_ALLOW_GENERIC_IMAGE) {
		/* TRANSLATORS: show the user a generic image that can be themed */
		return _("Image");
	}
	if (request_flag == FWUPD_REQUEST_FLAG_NON_GENERIC_MESSAGE) {
		/* TRANSLATORS: ask the user a question, and it will not be translated */
		return _("Message (custom)");
	}
	if (request_flag == FWUPD_REQUEST_FLAG_NON_GENERIC_IMAGE) {
		/* TRANSLATORS: show the user a random image from the internet */
		return _("Image (custom)");
	}
	return NULL;
}

gchar *
fu_cli_device_problem_to_string(FwupdClient *client, FwupdDevice *dev, FwupdDeviceProblem problem)
{
	if (problem == FWUPD_DEVICE_PROBLEM_NONE)
		return NULL;
	if (problem == FWUPD_DEVICE_PROBLEM_UNKNOWN)
		return NULL;
	if (problem == FWUPD_DEVICE_PROBLEM_SYSTEM_POWER_TOO_LOW) {
		if (fwupd_client_get_battery_level(client) == FWUPD_BATTERY_LEVEL_INVALID ||
		    fwupd_client_get_battery_threshold(client) == FWUPD_BATTERY_LEVEL_INVALID) {
			/* TRANSLATORS: as in laptop battery power */
			return g_strdup(_("System power is too low"));
		}
		return g_strdup_printf(
		    /* TRANSLATORS: as in laptop battery power */
		    _("System power is too low (%u%%, requires %u%%)"),
		    fwupd_client_get_battery_level(client),
		    fwupd_client_get_battery_threshold(client));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_UNREACHABLE) {
		/* TRANSLATORS: for example, a Bluetooth mouse that is in powersave mode */
		return g_strdup(_("Device is unreachable, or out of wireless range"));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_POWER_TOO_LOW) {
		if (fwupd_device_get_battery_level(dev) == FWUPD_BATTERY_LEVEL_INVALID ||
		    fwupd_device_get_battery_threshold(dev) == FWUPD_BATTERY_LEVEL_INVALID) {
			/* TRANSLATORS: for example the batteries *inside* the Bluetooth mouse */
			return g_strdup(_("Device battery power is too low"));
		}
		/* TRANSLATORS: for example the batteries *inside* the Bluetooth mouse */
		return g_strdup_printf(_("Device battery power is too low (%u%%, requires %u%%)"),
				       fwupd_device_get_battery_level(dev),
				       fwupd_device_get_battery_threshold(dev));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_UPDATE_PENDING) {
		/* TRANSLATORS: usually this is when we're waiting for a reboot */
		return g_strdup(_("Device is waiting for the update to be applied"));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_REQUIRE_AC_POWER) {
		/* TRANSLATORS: as in, wired mains power for a laptop */
		return g_strdup(_("Device requires AC power to be connected"));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_LID_IS_CLOSED) {
		/* TRANSLATORS: lid means "laptop top cover" */
		return g_strdup(_("Device cannot be updated while the lid is closed"));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_IS_EMULATED) {
		/* TRANSLATORS: emulated means we are pretending to be a different model */
		return g_strdup(_("Device is emulated"));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_MISSING_LICENSE) {
		/* TRANSLATORS: The device cannot be updated due to missing vendor's license. */
		return g_strdup(_("Device requires a software license to update"));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_SYSTEM_INHIBIT) {
		/* TRANSLATORS: an application is preventing system updates */
		return g_strdup(_("All devices are prevented from update by system inhibit"));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_UPDATE_IN_PROGRESS) {
		/* TRANSLATORS: another application is updating the device already */
		return g_strdup(_("An update is in progress"));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_IN_USE) {
		/* TRANSLATORS: device cannot be interrupted, for instance taking a phone call */
		return g_strdup(_("Device is in use"));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_DISPLAY_REQUIRED) {
		/* TRANSLATORS: device does not have a display connected */
		return g_strdup(_("Device requires a display to be plugged in"));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY) {
		/* TRANSLATORS: we have two ways of communicating with the device, so we hide one */
		return g_strdup(_("Device is lower priority than an equivalent device"));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_INSECURE_PLATFORM) {
		/* TRANSLATORS: firmware is signed with insecure key */
		return g_strdup(_("System has been signed with an insecure key"));
	}
	if (problem == FWUPD_DEVICE_PROBLEM_FIRMWARE_LOCKED) {
		/* TRANSLATORS: firmware is locked from the BIOS */
		return g_strdup(_("Device firmware has been locked"));
	}
	return NULL;
}

gchar *
fu_cli_plugin_flag_to_string(FwupdPluginFlags plugin_flag)
{
	if (plugin_flag == FWUPD_PLUGIN_FLAG_UNKNOWN)
		return NULL;
	if (plugin_flag == FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE)
		return NULL;
	if (plugin_flag == FWUPD_PLUGIN_FLAG_USER_WARNING)
		return NULL;
	if (plugin_flag == FWUPD_PLUGIN_FLAG_NONE)
		return NULL;
	if (plugin_flag == FWUPD_PLUGIN_FLAG_REQUIRE_HWID) {
		/* TRANSLATORS: Plugin is active only if hardware is found */
		return g_strdup(_("Enabled if hardware matches"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_READY) {
		/* TRANSLATORS: Plugin is active and in use */
		return g_strdup(_("Ready"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_DISABLED) {
		/* TRANSLATORS: Plugin is inactive and not used */
		return g_strdup(_("Disabled"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_NO_HARDWARE) {
		/* TRANSLATORS: not required for this system */
		return g_strdup(_("Required hardware was not found"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_LEGACY_BIOS) {
		/* TRANSLATORS: system is not booted in UEFI mode */
		return g_strdup(_("UEFI firmware can not be updated in legacy BIOS mode"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED) {
		return g_strdup(
		    /* TRANSLATORS: capsule updates are an optional BIOS feature */
		    _("UEFI capsule updates not available or enabled in firmware setup"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_UNLOCK_REQUIRED) {
		/* TRANSLATORS: user needs to run a command, %1 is 'fwupdmgr unlock' */
		return g_strdup_printf(_("Firmware updates disabled; run '%s' to enable"),
				       "fwupdmgr unlock");
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_AUTH_REQUIRED) {
		/* TRANSLATORS: user needs to run a command */
		return g_strdup(_("Authentication details are required"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_SECURE_CONFIG) {
		/* TRANSLATORS: no peeking */
		return g_strdup(_("Configuration is only readable by the system administrator"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_MODULAR) {
		/* TRANSLATORS: the plugin was created from a .so object, and was not built-in */
		return g_strdup(_("Loaded from an external module"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_MEASURE_SYSTEM_INTEGRITY) {
		/* TRANSLATORS: check various UEFI and ACPI tables are unchanged after the update */
		return g_strdup(_("Will measure elements of system integrity around an update"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_EFIVAR_NOT_MOUNTED) {
		/* TRANSLATORS: the user is using Gentoo/Arch and has screwed something up */
		return g_strdup(_("Required efivarfs filesystem was not found"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND) {
		/* TRANSLATORS: partition refers to something on disk, again, hey Arch users */
		return g_strdup(_("UEFI ESP partition not detected or configured"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_ESP_NOT_VALID) {
		/* TRANSLATORS: partition refers to something on disk, again, hey Arch users */
		return g_strdup(_("UEFI ESP partition may not be set up correctly"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_FAILED_OPEN) {
		/* TRANSLATORS: Failed to open plugin, hey Arch users */
		return g_strdup(_("Plugin dependencies missing"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_KERNEL_TOO_OLD) {
		/* TRANSLATORS: The kernel does not support this plugin */
		return g_strdup(_("Running kernel is too old"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_TEST_ONLY) {
		/* TRANSLATORS: The plugin is only for testing */
		return g_strdup(_("Plugin is only for testing"));
	}
	if (plugin_flag == FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION) {
		/* TRANSLATORS: The plugin enumeration might change the device current mode */
		return g_strdup(_("Plugin enumeration may change device state"));
	}

	/* fall back for unknown types */
	return g_strdup(fwupd_plugin_flag_to_string(plugin_flag));
}

const gchar *
fu_cli_release_flag_to_string(FwupdReleaseFlags release_flag)
{
	if (release_flag == FWUPD_RELEASE_FLAG_NONE)
		return NULL;
	if (release_flag == FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD) {
		/* TRANSLATORS: We verified the payload against the server */
		return _("Trusted payload");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_TRUSTED_METADATA) {
		/* TRANSLATORS: We verified the metadata against the server */
		return _("Trusted metadata");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_IS_UPGRADE) {
		/* TRANSLATORS: version is newer */
		return _("Is upgrade");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_IS_DOWNGRADE) {
		/* TRANSLATORS: version is older */
		return _("Is downgrade");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_BLOCKED_VERSION) {
		/* TRANSLATORS: version cannot be installed due to policy */
		return _("Blocked version");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_BLOCKED_APPROVAL) {
		/* TRANSLATORS: version cannot be installed due to policy */
		return _("Not approved");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_IS_ALTERNATE_BRANCH) {
		/* TRANSLATORS: is not the main firmware stream */
		return _("Alternate branch");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_IS_COMMUNITY) {
		/* TRANSLATORS: is not supported by the vendor */
		return _("Community supported");
	}
	if (release_flag == FWUPD_RELEASE_FLAG_TRUSTED_REPORT) {
		/* TRANSLATORS: someone we trust has tested this */
		return _("Tested by trusted vendor");
	}

	/* fall back for unknown types */
	return fwupd_release_flag_to_string(release_flag);
}

void
fu_cli_print_json_object(FuConsole *console, FwupdJsonObject *json_obj)
{
	g_autoptr(GString) str = NULL;
	str = fwupd_json_object_to_string(json_obj, FWUPD_JSON_EXPORT_FLAG_INDENT);
	fu_console_print_literal(console, str->str);
}

const gchar *
fu_cli_get_prgname(const gchar *argv0)
{
	const gchar *prgname = (const gchar *)g_strrstr(argv0, " ");
	if (prgname != NULL)
		return prgname + 1;
	return argv0;
}
