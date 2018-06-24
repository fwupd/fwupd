/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Dell, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <appstream-glib.h>
#include <smbios_c/token.h>
#include <smbios_c/smi.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

/* Whitelisted smbios class/select commands */
#define CLASS_ADMIN_PROP	10
#define SELECT_ADMIN_PROP	3

/* whitelisted tokens */
#define CAPSULE_EN_TOKEN	0x0461
#define CAPSULE_DIS_TOKEN	0x0462

/* these aren't defined upstream but used in fwupdate */
#define DELL_ADMIN_MASK 0xF
#define DELL_ADMIN_INSTALLED 0

static gboolean
fu_plugin_dell_esrt_query_token (guint16 token, gboolean *value, GError **error)
{
	if (!token_is_bool (token)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "token %" G_GUINT16_FORMAT " is not boolean",
			     token);
		return FALSE;
	}
	if (value != NULL)
		*value = token_is_active (token) > 0;

	return TRUE;
}

static gboolean
fu_plugin_dell_esrt_activate_token (guint16 token, GError **error)
{
	token_activate (token);
	if (token_is_active (token) < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "token %" G_GUINT16_FORMAT "cannot be activated "
			     "as the password is set", token);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_dell_esrt_admin_password_present (gboolean *password_present, GError **error)
{
	guint32 args[4] = { 0, }, out[4] = { 0, };

	if (dell_simple_ci_smi (CLASS_ADMIN_PROP,
			        SELECT_ADMIN_PROP, args, out)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "cannot call SMI for CLASS_ADMIN_PROP");
		return FALSE;
	}

	if (out[0] != 0 || (out[1] & DELL_ADMIN_MASK) == DELL_ADMIN_INSTALLED) {
		*password_present = TRUE;
	} else {
		*password_present = FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	gboolean capsule_disable = FALSE;
	gboolean password_present = FALSE;
	g_autofree gchar *sysfsfwdir = NULL;
	g_autofree gchar *esrtdir = NULL;

	/* already exists */
	sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	esrtdir = g_build_filename (sysfsfwdir, "efi", "esrt", NULL);
	if (g_file_test (esrtdir, G_FILE_TEST_EXISTS)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "UEFI firmware already supported");
		return FALSE;
	}

	/* is the capsule functionality disabled */
	if (!fu_plugin_dell_esrt_query_token (CAPSULE_DIS_TOKEN, &capsule_disable, error))
		return FALSE;
	if (!capsule_disable) {
		gboolean capsule_enable = FALSE;
		if (!fu_plugin_dell_esrt_query_token (CAPSULE_EN_TOKEN, &capsule_enable, error))
			return FALSE;
		if (capsule_enable) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "UEFI firmware can be unlocked on next boot");
			return FALSE;
		}
	}

	/* check the admin password isn't set */
	if (!fu_plugin_dell_esrt_admin_password_present (&password_present, error))
		return FALSE;
	if (password_present) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "cannot be enabled as admin password set");
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_plugin_unlock (FuPlugin *plugin, FuDevice *device, GError **error)
{
	/* disabled in BIOS, but supported to be enabled via tool */
	if (!fu_plugin_dell_esrt_query_token (CAPSULE_EN_TOKEN, NULL, error))
		return FALSE;
	return fu_plugin_dell_esrt_activate_token (CAPSULE_EN_TOKEN, error);
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	g_autoptr(FuDevice) dev = fu_device_new ();

	/* create a dummy device so we can unlock the feature */
	fu_device_set_id (dev, "UEFI-dummy-dev0");
	fu_device_set_name (dev, "UEFI dummy device");
	fu_device_add_guid (dev, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_set_version (dev, "0");
	fu_device_add_icon (dev, "computer");
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_LOCKED);
	fu_plugin_device_add (plugin, dev);
	return TRUE;
}
