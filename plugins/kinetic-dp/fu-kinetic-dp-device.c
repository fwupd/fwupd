/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 * Copyright (C) 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-kinetic-dp-device.h"

typedef struct {
	FuKineticDpFamily family;
	FuKineticDpChip chip_id;
	FuKineticDpFwState fw_state;
} FuKineticDpDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuKineticDpDevice, fu_kinetic_dp_device, FU_TYPE_DPAUX_DEVICE)
#define GET_PRIVATE(o) (fu_kinetic_dp_device_get_instance_private(o))

static void
fu_kinetic_dp_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuKineticDpDevice *self = FU_KINETIC_DP_DEVICE(device);
	FuKineticDpDevicePrivate *priv = GET_PRIVATE(self);
	fu_string_append(str, idt, "Family", fu_kinetic_dp_family_to_string(priv->family));
	fu_string_append(str, idt, "ChipId", fu_kinetic_dp_chip_to_string(priv->chip_id));
	fu_string_append(str, idt, "FwState", fu_kinetic_dp_fw_state_to_string(priv->fw_state));
}

void
fu_kinetic_dp_device_set_fw_state(FuKineticDpDevice *self, FuKineticDpFwState fw_state)
{
	FuKineticDpDevicePrivate *priv = GET_PRIVATE(self);
	priv->fw_state = fw_state;
}

FuKineticDpFwState
fu_kinetic_dp_device_get_fw_state(FuKineticDpDevice *self)
{
	FuKineticDpDevicePrivate *priv = GET_PRIVATE(self);
	return priv->fw_state;
}

void
fu_kinetic_dp_device_set_chip_id(FuKineticDpDevice *self, FuKineticDpChip chip_id)
{
	FuKineticDpDevicePrivate *priv = GET_PRIVATE(self);
	priv->chip_id = chip_id;
}

static FuKineticDpFamily
fu_kinetic_dp_device_chip_id_to_family(FuKineticDpChip chip_id)
{
	if (chip_id == FU_KINETIC_DP_CHIP_PUMA_2900 || chip_id == FU_KINETIC_DP_CHIP_PUMA_2920)
		return FU_KINETIC_DP_FAMILY_PUMA;
	if (chip_id == FU_KINETIC_DP_CHIP_MUSTANG_5200)
		return FU_KINETIC_DP_FAMILY_MUSTANG;
	if (chip_id == FU_KINETIC_DP_CHIP_JAGUAR_5000)
		return FU_KINETIC_DP_FAMILY_JAGUAR;
	return FU_KINETIC_DP_FAMILY_UNKNOWN;
}

static const gchar *
fu_kinetic_dp_device_get_name_for_chip_id(FuKineticDpChip chip_id)
{
	if (chip_id == FU_KINETIC_DP_CHIP_JAGUAR_5000)
		return "KTM50X0";
	if (chip_id == FU_KINETIC_DP_CHIP_MUSTANG_5200)
		return "KTM52X0";
	if (chip_id == FU_KINETIC_DP_CHIP_PUMA_2900)
		return "MC2900";
	return NULL;
}

gboolean
fu_kinetic_dp_device_dpcd_read_oui(FuKineticDpDevice *self,
				   guint8 *buf,
				   gsize bufsz,
				   GError **error)
{
	if (bufsz < DPCD_SIZE_IEEE_OUI) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "aux dpcd read buffer size [0x%x] is too small to read IEEE OUI",
			    (guint)bufsz);
		return FALSE;
	}
	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  DPCD_ADDR_IEEE_OUI,
				  buf,
				  DPCD_SIZE_IEEE_OUI,
				  FU_KINETIC_DP_DEVICE_TIMEOUT,
				  error)) {
		g_prefix_error(error, "aux dpcd read OUI failed: ");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_kinetic_dp_device_dpcd_write_oui(FuKineticDpDevice *self, const guint8 *buf, GError **error)
{
	if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
				   DPCD_ADDR_IEEE_OUI,
				   buf,
				   DPCD_SIZE_IEEE_OUI,
				   FU_KINETIC_DP_DEVICE_TIMEOUT,
				   error)) {
		g_prefix_error(error, "aux dpcd write OUI failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_kinetic_dp_device_setup(FuDevice *device, GError **error)
{
	FuKineticDpDevice *self = FU_KINETIC_DP_DEVICE(device);
	FuKineticDpDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *chip_id_str;

	/* FuDpauxDevice->setup */
	if (!FU_DEVICE_CLASS(fu_kinetic_dp_device_parent_class)->setup(device, error))
		return FALSE;

	/* sanity check */
	if (fu_dpaux_device_get_dpcd_ieee_oui(FU_DPAUX_DEVICE(device)) == 0x0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no IEEE OUI set");
		return FALSE;
	}

	/* set up the device name */
	chip_id_str = fu_kinetic_dp_device_get_name_for_chip_id(priv->chip_id);
	if (chip_id_str != NULL)
		fu_device_set_name(FU_DEVICE(self), chip_id_str);

	/* detect chip family */
	priv->family = fu_kinetic_dp_device_chip_id_to_family(priv->chip_id);
	fu_device_add_instance_strup(FU_DEVICE(self),
				     "FAM",
				     fu_kinetic_dp_family_to_string(priv->family));
	fu_device_add_instance_u16(FU_DEVICE(self),
				   "VEN",
				   fu_dpaux_device_get_dpcd_ieee_oui(FU_DPAUX_DEVICE(device)));
	fu_device_add_instance_str(FU_DEVICE(self),
				   "DEV",
				   fu_dpaux_device_get_dpcd_dev_id(FU_DPAUX_DEVICE(device)));
	fu_device_add_instance_str(FU_DEVICE(self), "CID", chip_id_str);
	if (!fu_device_build_instance_id_full(FU_DEVICE(self),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "MST",
					      "VEN",
					      "FAM",
					      NULL))
		return FALSE;
	if (!fu_device_build_instance_id_full(FU_DEVICE(self),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "MST",
					      "VEN",
					      "CID",
					      NULL))
		return FALSE;
	if (!fu_device_build_instance_id(FU_DEVICE(self), error, "MST", "VEN", "DEV", NULL))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_kinetic_dp_device_class_init(FuKineticDpDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_kinetic_dp_device_setup;
	klass_device->to_string = fu_kinetic_dp_device_to_string;
}

static void
fu_kinetic_dp_device_init(FuKineticDpDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.kinet-ic.dp");
	fu_device_set_vendor(FU_DEVICE(self), "Kinetic Technologies");
	fu_device_add_vendor_id(FU_DEVICE(self), "DRM_DP_AUX_DEV:0x329A");
	fu_device_set_summary(FU_DEVICE(self), "DisplayPort Protocol Converter");
	fu_device_add_icon(FU_DEVICE(self), "video-display");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_NO_GENERIC_GUIDS);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
				 FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_WRITE);
}
