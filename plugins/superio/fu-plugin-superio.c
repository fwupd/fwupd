/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-superio-it85-device.h"
#include "fu-superio-it89-device.h"

#define		FU_QUIRKS_SUPERIO_CHIPSETS		"SuperioChipsets"

static gboolean
fu_plugin_superio_coldplug_chipset (FuPlugin *plugin, const gchar *chipset, GError **error)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	const gchar *dmi_vendor;
	g_autoptr(FuSuperioDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* create IT89xx or IT89xx */
	if (g_strcmp0 (chipset, "IT8587") == 0) {
		dev = g_object_new (FU_TYPE_SUPERIO_IT85_DEVICE,
				    "device-file", "/dev/port",
				    "chipset", chipset,
				    "context", ctx,
				    NULL);
	} else if (g_strcmp0 (chipset, "IT8987") == 0) {
		dev = g_object_new (FU_TYPE_SUPERIO_IT89_DEVICE,
				    "device-file", "/dev/port",
				    "chipset", chipset,
				    "context", ctx,
				    NULL);
	} else {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "SuperIO chip %s has unsupported SuperioId", chipset);
		return FALSE;
	}

	/* set ID and port via quirks */
	if (!fu_device_probe (FU_DEVICE (dev), error))
		return FALSE;

	/* set vendor ID as the motherboard vendor */
	dmi_vendor = fu_context_get_hwid_value (ctx, FU_HWIDS_KEY_BASEBOARD_MANUFACTURER);
	if (dmi_vendor != NULL) {
		g_autofree gchar *vendor_id = g_strdup_printf ("DMI:%s", dmi_vendor);
		fu_device_add_vendor_id (FU_DEVICE (dev), vendor_id);
	}

	/* unlock */
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add (plugin, FU_DEVICE (dev));

	return TRUE;
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "linux_lockdown");
	fu_context_add_quirk_key (ctx, "SuperioChipsets");
	fu_context_add_quirk_key (ctx, "SuperioId");
	fu_context_add_quirk_key (ctx, "SuperioPort");
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
	for (guint i = 0; i < hwids->len; i++) {
		const gchar *tmp;
		const gchar *guid = g_ptr_array_index (hwids, i);
		g_autofree gchar *key = g_strdup_printf ("%s", guid);
		tmp = fu_context_lookup_quirk_by_id (ctx, key, FU_QUIRKS_SUPERIO_CHIPSETS);
		if (tmp == NULL)
			continue;
		if (!fu_plugin_superio_coldplug_chipset (plugin, tmp, error))
			return FALSE;
	}
	return TRUE;
}
