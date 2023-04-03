/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021, TUXEDO Computers GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-superio-it55-device.h"
#include "fu-superio-it85-device.h"
#include "fu-superio-it89-device.h"
#include "fu-superio-plugin.h"

struct _FuSuperioPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSuperioPlugin, fu_superio_plugin, FU_TYPE_PLUGIN)

#define FU_QUIRKS_SUPERIO_GTYPE "SuperioGType"

static gboolean
fu_superio_plugin_coldplug_chipset(FuPlugin *plugin, const gchar *guid, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *dmi_vendor;
	const gchar *chipset;
	GType custom_gtype;
	g_autoptr(FuSuperioDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* get chipset */
	chipset = fu_context_lookup_quirk_by_id(ctx, guid, FU_QUIRKS_SUPERIO_GTYPE);
	if (chipset == NULL)
		return TRUE;

	/* create IT89xx, IT89xx or IT5570 */
	custom_gtype = g_type_from_name(chipset);
	if (custom_gtype == G_TYPE_INVALID) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "SuperIO GType %s unsupported",
			    chipset);
		return FALSE;
	}
	dev = g_object_new(custom_gtype,
			   "device-file",
			   "/dev/port",
			   "chipset",
			   chipset,
			   "context",
			   ctx,
			   NULL);

	/* add this so we can attach all the other quirks */
	fu_device_add_instance_str(FU_DEVICE(dev), "GUID", guid);
	if (!fu_device_build_instance_id(FU_DEVICE(dev), error, "SUPERIO", "GUID", NULL))
		return FALSE;

	/* set ID and ports via quirks */
	if (!fu_device_probe(FU_DEVICE(dev), error))
		return FALSE;

	/* set vendor ID as the motherboard vendor */
	dmi_vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BASEBOARD_MANUFACTURER);
	if (dmi_vendor != NULL) {
		g_autofree gchar *vendor_id = g_strdup_printf("DMI:%s", dmi_vendor);
		fu_device_add_vendor_id(FU_DEVICE(dev), vendor_id);
	}

	/* unlock */
	locker = fu_device_locker_new(dev, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add(plugin, FU_DEVICE(dev));

	return TRUE;
}

static gboolean
fu_superio_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	GPtrArray *hwids;

	if (fu_kernel_locked_down()) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not supported when kernel locked down");
		return FALSE;
	}

	hwids = fu_context_get_hwid_guids(ctx);
	for (guint i = 0; i < hwids->len; i++) {
		const gchar *guid = g_ptr_array_index(hwids, i);
		if (!fu_superio_plugin_coldplug_chipset(plugin, guid, error))
			return FALSE;
	}
	return TRUE;
}

static void
fu_superio_plugin_init(FuSuperioPlugin *self)
{
}

static void
fu_superio_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "SuperioGType");
	fu_context_add_quirk_key(ctx, "SuperioId");
	fu_context_add_quirk_key(ctx, "SuperioPort");
	fu_context_add_quirk_key(ctx, "SuperioControlPort");
	fu_context_add_quirk_key(ctx, "SuperioDataPort");
	fu_context_add_quirk_key(ctx, "SuperioTimeout");
	fu_context_add_quirk_key(ctx, "SuperioAutoloadAction");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_EC_IT55_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SUPERIO_IT85_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SUPERIO_IT89_DEVICE);
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "linux_lockdown");
}

static void
fu_superio_plugin_class_init(FuSuperioPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_superio_plugin_constructed;
	plugin_class->coldplug = fu_superio_plugin_coldplug;
}
