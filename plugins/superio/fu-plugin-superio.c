/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#include "fu-superio-it85-device.h"
#include "fu-superio-it89-device.h"

#define		FU_QUIRKS_SUPERIO_CHIPSETS		"SuperioChipsets"

static gboolean
fu_plugin_superio_coldplug_chipset (FuPlugin *plugin, const gchar *chipset, GError **error)
{
	const gchar *dmi_vendor;
	g_autoptr(FuSuperioDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autofree gchar *key = g_strdup_printf ("SuperIO=%s", chipset);
	guint64 id;
	guint64 port;

	/* get ID we need for the chipset */
	id = fu_plugin_lookup_quirk_by_id_as_uint64 (plugin, key, "Id");
	if (id == 0x0000 || id > 0xffff) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "SuperIO chip %s has invalid Id", chipset);
		return FALSE;
	}

	/* set address */
	port = fu_plugin_lookup_quirk_by_id_as_uint64 (plugin, key, "Port");
	if (port == 0x0 || port > 0xffff) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "SuperIO chip %s has invalid Port", chipset);
		return FALSE;
	}

	/* create IT89xx or IT89xx */
	if (id >> 8 == 0x85) {
		dev = g_object_new (FU_TYPE_SUPERIO_IT85_DEVICE,
				    "device-file", "/dev/port",
				    "chipset", chipset,
				    "id", id,
				    "port", port,
				    NULL);
	} else if (id >> 8 == 0x89) {
		dev = g_object_new (FU_TYPE_SUPERIO_IT89_DEVICE,
				    "device-file", "/dev/port",
				    "chipset", chipset,
				    "id", id,
				    "port", port,
				    NULL);
	} else {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "SuperIO chip %s has unsupported Id", chipset);
		return FALSE;
	}

	/* set vendor ID as the motherboard vendor */
	dmi_vendor = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BASEBOARD_MANUFACTURER);
	if (dmi_vendor != NULL) {
		g_autofree gchar *vendor_id = g_strdup_printf ("DMI:%s", dmi_vendor);
		fu_device_set_vendor_id (FU_DEVICE (dev), vendor_id);
	}

	/* unlock */
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add (plugin, FU_DEVICE (dev));

	return TRUE;
}

static gboolean
fu_plugin_superio_coldplug_chipsets (FuPlugin *plugin, const gchar *str, GError **error)
{
	g_auto(GStrv) chipsets = g_strsplit (str, ",", -1);
	for (guint i = 0; chipsets[i] != NULL; i++) {
		if (!fu_plugin_superio_coldplug_chipset (plugin, chipsets[i], error))
			return FALSE;
	}
	return TRUE;
}

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	GPtrArray *hwids;

	if (fu_common_kernel_locked_down ()) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported when kernel locked down");
		return FALSE;
	}

	hwids = fu_plugin_get_hwids (plugin);
	for (guint i = 0; i < hwids->len; i++) {
		const gchar *tmp;
		const gchar *guid = g_ptr_array_index (hwids, i);
		g_autofree gchar *key = g_strdup_printf ("HwId=%s", guid);
		tmp = fu_plugin_lookup_quirk_by_id (plugin, key, FU_QUIRKS_SUPERIO_CHIPSETS);
		if (tmp == NULL)
			continue;
		if (!fu_plugin_superio_coldplug_chipsets (plugin, tmp, error))
			return FALSE;
	}
	return TRUE;
}
