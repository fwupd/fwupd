/*
 * Copyright 2022 Intel
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR Apache-2.0
 */

#include "config.h"

#include "fu-igsc-aux-device.h"
#include "fu-igsc-aux-firmware.h"
#include "fu-igsc-code-firmware.h"
#include "fu-igsc-device.h"
#include "fu-igsc-oprom-device.h"
#include "fu-igsc-oprom-firmware.h"
#include "fu-igsc-plugin.h"

#define FU_IGSC_PLUGIN_POWER_WRITE_TIMEOUT 1500 /* ms */

struct _FuIgscPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuIgscPlugin, fu_igsc_plugin, FU_TYPE_PLUGIN)

static void
fu_igsc_plugin_init(FuIgscPlugin *self)
{
}

static gboolean
fu_igsc_plugin_set_pci_power_policy(FuIgscDevice *self, const gchar *val, GError **error)
{
	g_autoptr(FuDevice) parent = NULL;

	/* get PCI parent */
	parent = fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), "pci", error);
	if (parent == NULL)
		return FALSE;
	return fu_udev_device_write_sysfs(FU_UDEV_DEVICE(parent),
					  "power/control",
					  val,
					  FU_IGSC_PLUGIN_POWER_WRITE_TIMEOUT,
					  error);
}

static gboolean
fu_igsc_plugin_composite_prepare(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuIgscDevice *device_igsc = NULL;
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (FU_IS_IGSC_DEVICE(device)) {
			device_igsc = FU_IGSC_DEVICE(device);
			break;
		}
	}
	if (device_igsc != NULL) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_igsc_plugin_set_pci_power_policy(device_igsc, "on", &error_local))
			g_debug("failed to set power policy: %s", error_local->message);
	}
	return TRUE;
}

static gboolean
fu_igsc_plugin_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuIgscDevice *device_igsc = NULL;
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (FU_IS_IGSC_DEVICE(device)) {
			device_igsc = FU_IGSC_DEVICE(device);
			break;
		}
	}
	if (device_igsc != NULL) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_igsc_plugin_set_pci_power_policy(device_igsc, "off", &error_local))
			g_debug("failed to set power policy: %s", error_local->message);
	}
	return TRUE;
}

static void
fu_igsc_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "mei");
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_IGSC_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_IGSC_OPROM_DEVICE); /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_IGSC_AUX_DEVICE);   /* coverage */
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_IGSC_CODE_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_IGSC_AUX_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_IGSC_OPROM_FIRMWARE);
}

static void
fu_igsc_plugin_class_init(FuIgscPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_igsc_plugin_constructed;
	plugin_class->composite_prepare = fu_igsc_plugin_composite_prepare;
	plugin_class->composite_cleanup = fu_igsc_plugin_composite_cleanup;
}
