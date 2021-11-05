/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-mtd-recovery-device.h"
#include "fu-mtd-recovery-plugin.h"

struct _FuMtdRecoveryPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuMtdRecoveryPlugin, fu_mtd_recovery_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_mtd_recovery_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	const gchar *product;
	const gchar *vendor;
	g_autoptr(FuDevice) device = NULL;

	device = g_object_new(FU_TYPE_MTD_RECOVERY_DEVICE, "context", ctx, NULL);

	/* set vendor ID as the baseboard vendor */
	vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BASEBOARD_MANUFACTURER);
	if (vendor != NULL) {
		g_autofree gchar *vendor_id = g_strdup_printf("DMI:%s", vendor);
		fu_device_add_vendor_id(device, vendor_id);
		fu_device_add_instance_strsafe(device, "VEN", vendor);
		if (!fu_device_build_instance_id_quirk(device, error, "MTD", "VEN", NULL))
			return FALSE;
	}

	/* set instance ID as the baseboard vendor and product */
	product = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BASEBOARD_PRODUCT);
	if (product != NULL) {
		fu_device_add_instance_strsafe(device, "DEV", product);
		if (!fu_device_build_instance_id(device, error, "MTD", "VEN", "DEV", NULL))
			return FALSE;
	}

	/* manually convert the IDs */
	if (!fu_device_setup(device, error))
		return FALSE;

	/* success */
	fu_plugin_device_add(plugin, device);
	return TRUE;
}

static void
fu_mtd_recovery_plugin_set_proxy(FuPlugin *plugin, FuDevice *device)
{
	GPtrArray *devices = fu_plugin_get_devices(plugin);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		g_debug("using %s as proxy to %s",
			fu_device_get_id(device),
			fu_device_get_id(device_tmp));
		fu_device_set_proxy(device_tmp, device);
	}
}

/* a MTD device just showed up, probably as the result of FuMtdRecoveryDevice->detach() */
static void
fu_mtd_recovery_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	if (g_strcmp0(fu_device_get_plugin(device), "mtd") == 0 &&
	    fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE)) {
		fu_mtd_recovery_plugin_set_proxy(plugin, device);
		fu_device_inhibit(device, "proxy-to-recovery", "Proxy for recovery device");
	}
}

/* a MTD device got removed, probably as the result of FuMtdRecoveryDevice->attach() */
static gboolean
fu_mtd_recovery_plugin_backend_device_removed(FuPlugin *plugin, FuDevice *device, GError **error)
{
	if (g_strcmp0(fu_device_get_plugin(device), "mtd") == 0)
		fu_mtd_recovery_plugin_set_proxy(plugin, NULL);
	return TRUE;
}

static void
fu_mtd_recovery_plugin_init(FuMtdRecoveryPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_REQUIRE_HWID);
}

static void
fu_mtd_recovery_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "MtdRecoveryGpioNumber");
	fu_context_add_quirk_key(ctx, "MtdRecoveryKernelDriver");
	fu_context_add_quirk_key(ctx, "MtdRecoveryBindId");
}

static void
fu_mtd_recovery_plugin_class_init(FuMtdRecoveryPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_mtd_recovery_plugin_constructed;
	plugin_class->coldplug = fu_mtd_recovery_plugin_coldplug;
	plugin_class->device_registered = fu_mtd_recovery_plugin_device_registered;
	plugin_class->backend_device_removed = fu_mtd_recovery_plugin_backend_device_removed;
}
