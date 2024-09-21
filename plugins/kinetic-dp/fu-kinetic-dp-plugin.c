/*
 * Copyright 2017 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright 2017 Peichen Huang <peichenhuang@tw.kinetic.com>
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 * Copyright 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-kinetic-dp-plugin.h"
#include "fu-kinetic-dp-puma-device.h"
#include "fu-kinetic-dp-puma-firmware.h"
#include "fu-kinetic-dp-secure-device.h"
#include "fu-kinetic-dp-secure-firmware.h"
#include "fu-kinetic-dp-struct.h"

struct _FuKineticDpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuKineticDpPlugin, fu_kinetic_dp_plugin, FU_TYPE_PLUGIN)

static FuKineticDpDevice *
fu_kinetic_dp_plugin_create_device(FuDpauxDevice *dpaux_device, GError **error)
{
	FuKineticDpChip chip_id = 0;
	FuKineticDpFwState fw_state = 0;
	const gchar *dev_id = fu_dpaux_device_get_dpcd_dev_id(dpaux_device);
	g_autoptr(FuKineticDpDevice) dp_device = NULL;
	const struct {
		FuKineticDpChip chip_id;
		FuKineticDpFwState fw_state;
		const gchar *id_str;
	} map[] = {{FU_KINETIC_DP_CHIP_JAGUAR_5000, FU_KINETIC_DP_FW_STATE_IROM, "5010IR"},
		   {FU_KINETIC_DP_CHIP_JAGUAR_5000, FU_KINETIC_DP_FW_STATE_APP, "KT50X0"},
		   {FU_KINETIC_DP_CHIP_MUSTANG_5200, FU_KINETIC_DP_FW_STATE_IROM, "5210IR"},
		   {FU_KINETIC_DP_CHIP_MUSTANG_5200, FU_KINETIC_DP_FW_STATE_APP, "KT52X0"},
		   {FU_KINETIC_DP_CHIP_MUSTANG_5200, FU_KINETIC_DP_FW_STATE_APP, "KT5200"},
		   {FU_KINETIC_DP_CHIP_PUMA_2900, FU_KINETIC_DP_FW_STATE_IROM, "PUMA"},
		   {FU_KINETIC_DP_CHIP_PUMA_2900, FU_KINETIC_DP_FW_STATE_APP, "MC290"},
		   {FU_KINETIC_DP_CHIP_PUMA_2900, FU_KINETIC_DP_FW_STATE_APP, "MC2910"}};

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* sanity check */
	if (dev_id == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device has no DPCD device id");
		return NULL;
	}

	/* find the device info by branch ID string */
	for (guint32 i = 0; i < G_N_ELEMENTS(map); i++) {
		if (strncmp(dev_id, map[i].id_str, strlen(map[i].id_str)) == 0) {
			chip_id = map[i].chip_id;
			fw_state = map[i].fw_state;
			break;
		}
	}

	/* use the corresponding GType */
	if (chip_id == FU_KINETIC_DP_CHIP_JAGUAR_5000 ||
	    chip_id == FU_KINETIC_DP_CHIP_MUSTANG_5200) {
		dp_device =
		    FU_KINETIC_DP_DEVICE(g_object_new(FU_TYPE_KINETIC_DP_SECURE_DEVICE, NULL));
	} else if (chip_id == FU_KINETIC_DP_CHIP_PUMA_2900) {
		dp_device =
		    FU_KINETIC_DP_DEVICE(g_object_new(FU_TYPE_KINETIC_DP_PUMA_DEVICE, NULL));
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s is not a supported Kinetic device",
			    dev_id);
		return NULL;
	}
	fu_device_incorporate(FU_DEVICE(dp_device),
			      FU_DEVICE(dpaux_device),
			      FU_DEVICE_INCORPORATE_FLAG_ALL);
	fu_kinetic_dp_device_set_chip_id(dp_device, chip_id);
	fu_kinetic_dp_device_set_fw_state(dp_device, fw_state);
	return g_steal_pointer(&dp_device);
}

static gboolean
fu_kinetic_dp_plugin_backend_device_added(FuPlugin *plugin,
					  FuDevice *device,
					  FuProgress *progress,
					  GError **error)
{
	FuKineticDpPlugin *self = FU_KINETIC_DP_PLUGIN(plugin);
	g_autoptr(FuKineticDpDevice) dev = NULL;

	/* check to see if this is device we care about? */
	if (!FU_IS_DPAUX_DEVICE(device))
		return TRUE;

	/* instantiate a new device */
	dev = fu_kinetic_dp_plugin_create_device(FU_DPAUX_DEVICE(device), error);
	if (dev == NULL)
		return FALSE;
	fu_plugin_device_add(FU_PLUGIN(self), FU_DEVICE(dev));
	return TRUE;
}

static void
fu_kinetic_dp_plugin_init(FuKineticDpPlugin *self)
{
}

static void
fu_kinetic_dp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "drm"); /* used for uevent only */
	fu_plugin_add_device_udev_subsystem(plugin, "drm_dp_aux_dev");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_KINETIC_DP_PUMA_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_KINETIC_DP_SECURE_FIRMWARE);
}

static void
fu_kinetic_dp_plugin_class_init(FuKineticDpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_kinetic_dp_plugin_constructed;
	plugin_class->backend_device_added = fu_kinetic_dp_plugin_backend_device_added;
}
