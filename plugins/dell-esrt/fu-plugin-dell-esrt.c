/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Dell, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <fcntl.h>
#include <smbios_c/smi.h>
#include <smbios_c/token.h>
#include <sys/stat.h>
#include <sys/types.h>

/* allowed smbios class/select commands */
#define CLASS_ADMIN_PROP  10
#define SELECT_ADMIN_PROP 3

/* allowed tokens */
#define CAPSULE_EN_TOKEN  0x0461
#define CAPSULE_DIS_TOKEN 0x0462

/* these aren't defined upstream but used in fwupdate */
#define DELL_ADMIN_MASK	     0xF
#define DELL_ADMIN_INSTALLED 0

static gboolean
fu_plugin_dell_esrt_query_token(guint16 token, gboolean *value, GError **error)
{
	if (!token_is_bool(token)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "token %" G_GUINT16_FORMAT " is not boolean",
			    token);
		return FALSE;
	}
	if (value != NULL)
		*value = token_is_active(token) > 0;

	return TRUE;
}

static gboolean
fu_plugin_dell_esrt_activate_token(guint16 token, GError **error)
{
	token_activate(token);
	if (token_is_active(token) < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "token %" G_GUINT16_FORMAT "cannot be activated "
			    "as the password is set",
			    token);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_dell_esrt_admin_password_present(gboolean *password_present, GError **error)
{
	guint32 args[4] =
	    {
		0,
	    },
		out[4] = {
		    0,
		};

	if (dell_simple_ci_smi(CLASS_ADMIN_PROP, SELECT_ADMIN_PROP, args, out)) {
		g_set_error_literal(error,
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

static void
fu_plugin_dell_esrt_init(FuPlugin *plugin)
{
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_BETTER_THAN, "bios");
}

static gboolean
fu_plugin_dell_esrt_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	gboolean capsule_disable = FALSE;
	g_autofree gchar *sysfsfwdir = NULL;
	g_autofree gchar *esrtdir = NULL;

	/* already exists */
	sysfsfwdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	esrtdir = g_build_filename(sysfsfwdir, "efi", "esrt", NULL);
	if (g_file_test(esrtdir, G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "UEFI firmware already supported");
		return FALSE;
	}

	/* is the capsule functionality disabled */
	if (!fu_plugin_dell_esrt_query_token(CAPSULE_DIS_TOKEN, &capsule_disable, error))
		return FALSE;
	if (!capsule_disable) {
		gboolean capsule_enable = FALSE;
		if (!fu_plugin_dell_esrt_query_token(CAPSULE_EN_TOKEN, &capsule_enable, error))
			return FALSE;
		if (capsule_enable) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "UEFI firmware will be unlocked on next boot");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_plugin_dell_esrt_unlock(FuPlugin *plugin, FuDevice *device, GError **error)
{
	gboolean password_present = FALSE;
	/* check the admin password isn't set */
	if (!fu_plugin_dell_esrt_admin_password_present(&password_present, error))
		return FALSE;
	if (password_present) {
		const gchar *err_string = "Cannot be unlocked automatically as admin password set";
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, err_string);
		fu_device_set_update_error(device, err_string);
		return FALSE;
	}

	/* disabled in BIOS, but supported to be enabled via tool */
	if (!fu_plugin_dell_esrt_query_token(CAPSULE_EN_TOKEN, NULL, error))
		return FALSE;
	if (!fu_plugin_dell_esrt_activate_token(CAPSULE_EN_TOKEN, error))
		return FALSE;
	fu_device_set_update_error(device, NULL);

	return TRUE;
}

static gboolean
fu_plugin_dell_esrt_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	g_autoptr(FuDevice) dev = fu_device_new(NULL);

	/* create a dummy device so we can unlock the feature */
	fu_device_set_id(dev, "UEFI-dummy");
	fu_device_set_name(dev, "Dell UEFI updates");
	fu_device_set_summary(dev, "UEFI update functionality");
	fu_device_add_vendor_id(dev, "PCI:0x1028");
	fu_device_add_instance_id(dev, "main-system-firmware");
	fu_device_add_guid(dev, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
	fu_device_set_version_format(dev, FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_version(dev, "0");
	fu_device_add_icon(dev, "computer");
	fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_set_update_error(dev,
				   "Firmware updates disabled; run 'fwupdmgr unlock' to enable");
	if (!fu_device_setup(dev, error))
		return FALSE;
	fu_plugin_device_add(plugin, dev);
	return TRUE;
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_dell_esrt_init;
	vfuncs->startup = fu_plugin_dell_esrt_startup;
	vfuncs->coldplug = fu_plugin_dell_esrt_coldplug;
	vfuncs->unlock = fu_plugin_dell_esrt_unlock;
}
