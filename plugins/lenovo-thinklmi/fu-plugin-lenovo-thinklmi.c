/*
 * Copyright (C) 2021 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	g_autofree gchar *sysfsfwdir = NULL;
	g_autofree gchar *thinklmidir = NULL;

	/* already exists */
	sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW_ATTRIB);
	thinklmidir = g_build_filename (sysfsfwdir, "thinklmi", NULL);
	if (!g_file_test (thinklmidir, G_FILE_TEST_EXISTS)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "thinklmi not available");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_plugin_lenovo_firmware_pending_change (gboolean *result, GError **error)
{
	g_autofree gchar *buf = NULL;
	g_autofree gchar *sysfsfwdir = NULL;
	g_autofree gchar *pending = NULL;
	sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW_ATTRIB);
	pending = g_build_filename (sysfsfwdir, "thinklmi", "attributes",
				    "pending_reboot", NULL);

	/* we can't check, assume not locked */
	if (!g_file_test (pending, G_FILE_TEST_EXISTS))
		return TRUE;

	if (!g_file_get_contents (pending, &buf, NULL, error)) {
		g_prefix_error (error, "failed to get %s: ", pending);
		return FALSE;
	}
	*result = fu_common_strtoull (buf) > 0;

	return TRUE;
}

static gboolean
fu_plugin_lenovo_firmware_locked (gboolean *locked, GError **error)
{
	g_autofree gchar *buf = NULL;
	g_autofree gchar *sysfsfwdir = NULL;
	g_autofree gchar *thinklmi = NULL;

	sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW_ATTRIB);
	thinklmi = g_build_filename (sysfsfwdir, "thinklmi", "attributes",
				     "BootOrderLock", "current_value", NULL);

	/* we can't check, assume not locked */
	if (!g_file_test (thinklmi, G_FILE_TEST_EXISTS))
		return TRUE;

	if (!g_file_get_contents (thinklmi, &buf, NULL, error)) {
		g_prefix_error (error, "failed to get %s: ", thinklmi);
		return FALSE;
	}
	*locked = g_strcmp0 (g_strchomp (buf), "Enable") == 0;

	return TRUE;
}

void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	gboolean locked = FALSE;
	gboolean pending = FALSE;
	g_autoptr(GError) error_locked = NULL;
	g_autoptr(GError) error_pending = NULL;
	if (g_strcmp0 (fu_device_get_plugin (device), "uefi_capsule") != 0)
		return;
	if (!fu_plugin_lenovo_firmware_locked (&locked, &error_locked)) {
		g_debug ("%s", error_locked->message);
		return;
	}
	if (!fu_plugin_lenovo_firmware_pending_change (&pending, &error_pending)) {
		g_debug ("%s", error_pending->message);
		return;
	}

	if (locked)
		fu_device_inhibit (device,
				   "uefi-capsule-bootorder",
				   "BootOrder is locked in firmware setup");
	if (pending)
		fu_device_inhibit (device,
				   "uefi-capsule-pending-reboot",
				   "UEFI BIOS settings update pending reboot");
}
