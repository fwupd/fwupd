/*
 * Copyright (C) 2021, TUXEDO Computers GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-ec-common.h"
#include "fu-ec-it5570-device.h"

#define FU_QUIRKS_EC_CHIPSETS	"EcChipsets"

static void
fu_plugin_ec_load_config (FuPlugin *plugin, FuDevice *device)
{
	gboolean do_not_require_ac;
	int autoload_action_int = AUTOLOAD_NO_ACTION;
	g_autofree gchar *autoload_action = NULL;

	/* what to do with Autoload feature */
	autoload_action = fu_plugin_get_config_value (plugin, "AutoloadAction");
	if (g_strcmp0 (autoload_action, "none") == 0) {
		autoload_action_int = AUTOLOAD_NO_ACTION;
	} else if (g_strcmp0 (autoload_action, "disable") == 0) {
		autoload_action_int = AUTOLOAD_DISABLE;
	} else if (g_strcmp0 (autoload_action, "seton") == 0) {
		autoload_action_int = AUTOLOAD_SET_ON;
	} else if (g_strcmp0 (autoload_action, "setoff") == 0) {
		autoload_action_int = AUTOLOAD_SET_OFF;
	}
	fu_device_set_metadata_integer (device, "AutoloadAction", autoload_action_int);

	do_not_require_ac = fu_plugin_get_config_value_boolean (plugin, "DoNotRequireAC");
	fu_device_set_metadata_boolean (device,
					"RequireAC",
					!do_not_require_ac);
}

static gboolean
fu_plugin_ec_coldplug_chipset (FuPlugin *plugin, const gchar *chipset, GError **error)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	const gchar *dmi_vendor;
	g_autoptr(FuEcDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* create IT5570 */
	if (g_strcmp0 (chipset, "IT5570") == 0) {
		dev = g_object_new (FU_TYPE_EC_IT5570_DEVICE,
				    "device-file", "/dev/port",
				    "chipset", chipset,
				    "context", ctx,
				    NULL);
	} else {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "EC chip %s is not supported", chipset);
		return FALSE;
	}

	/* load all configuration variables */
	fu_plugin_ec_load_config (plugin, FU_DEVICE (dev));

	/* set ports via quirks */
	if (!fu_device_probe (FU_DEVICE (dev), error))
		return FALSE;

	/* set vendor ID as the motherboard vendor */
	dmi_vendor = fu_context_get_hwid_value (ctx, FU_HWIDS_KEY_BASEBOARD_MANUFACTURER);
	if (dmi_vendor != NULL) {
		g_autofree gchar *vendor_id = g_strdup_printf ("DMI:%s", dmi_vendor);
		fu_device_add_vendor_id (FU_DEVICE (dev), vendor_id);
	}

	/* probe the device to configure GUIDs */
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;

	/* register device */
	fu_plugin_device_add (plugin, FU_DEVICE (dev));

	/* success */
	return TRUE;
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "linux_lockdown");
	fu_context_add_quirk_key (ctx, FU_QUIRKS_EC_CHIPSETS);
	fu_context_add_quirk_key (ctx, "EcControlPort");
	fu_context_add_quirk_key (ctx, "EcDataPort");
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	GPtrArray *hwids;

	if (fu_common_kernel_locked_down ()) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported when kernel locked down");
		return FALSE;
	}

	hwids = fu_context_get_hwid_guids (ctx);
	for (guint i = 0; i < hwids->len; ++i) {
		const gchar *chipset;
		const gchar *guid = g_ptr_array_index (hwids, i);
		chipset = fu_context_lookup_quirk_by_id (ctx, guid, FU_QUIRKS_EC_CHIPSETS);
		if (chipset == NULL)
			continue;
		if (!fu_plugin_ec_coldplug_chipset (plugin, chipset, error))
			return FALSE;
	}
	return TRUE;
}
