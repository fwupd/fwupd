/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>

#include "fu-kinetic-dp-common.h"
#include "fu-kinetic-dp-connection.h"
#include "fu-kinetic-dp-device.h"
#include "fu-kinetic-dp-firmware.h"
#include "fu-kinetic-dp-secure-aux-isp.h"

struct _FuKineticDpDevice {
	FuUdevDevice parent_instance;
	FuKineticDpAuxIsp *aux_isp_ctrl;
	gchar *system_type;
	FuKineticDpFamily family;
	FuKineticDpMode mode;
};

G_DEFINE_TYPE(FuKineticDpDevice, fu_kinetic_dp_device, FU_TYPE_UDEV_DEVICE)

FuKineticDpAuxIsp *
fu_kinetic_dp_device_get_aux_isp_ctrl(FuKineticDpDevice *self)
{
	g_return_val_if_fail(FU_IS_DEVICE(self), NULL);
	return self->aux_isp_ctrl;
}

void
fu_kinetic_dp_device_set_aux_isp_ctrl(FuKineticDpDevice *self, KtChipId chip_id)
{
	g_return_if_fail(FU_IS_KINETIC_DP_DEVICE(self));
	g_return_if_fail(self->aux_isp_ctrl == NULL);

	if (chip_id == KT_CHIP_JAGUAR_5000 || chip_id == KT_CHIP_MUSTANG_5200)
		g_set_object(&self->aux_isp_ctrl,
			     FU_KINETIC_DP_AUX_ISP(fu_kinetic_dp_secure_aux_isp_new()));
}

void
fu_kinetic_dp_device_set_system_type(FuKineticDpDevice *self, const gchar *system_type)
{
	g_return_if_fail(FU_IS_KINETIC_DP_DEVICE(self));
	self->system_type = g_strdup(system_type);
}

static void
fu_kinetic_dp_device_init(FuKineticDpDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.kinet-ic.dp");
	fu_device_set_vendor(FU_DEVICE(self), "Kinetic Technologies");
	fu_device_add_vendor_id(FU_DEVICE(self), "DRM_DP_AUX_DEV:0x329A");
	fu_device_set_summary(FU_DEVICE(self), "DisplayPort Protocol Converter");
	fu_device_add_icon(FU_DEVICE(self), "video-display");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
				 FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
				     FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT);
}

static void
fu_kinetic_dp_device_finalize(GObject *object)
{
	FuKineticDpDevice *self = FU_KINETIC_DP_DEVICE(object);

	if (self->aux_isp_ctrl != NULL)
		g_object_unref(self->aux_isp_ctrl);
	g_free(self->system_type);

	G_OBJECT_CLASS(fu_kinetic_dp_device_parent_class)->finalize(object);
}

static gboolean
fu_kinetic_dp_device_probe(FuDevice *device, GError **error)
{
	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_kinetic_dp_device_parent_class)->probe(device, error))
		return FALSE;

	/* get from sysfs if not set from tests */
	if (fu_device_get_logical_id(device) == NULL) {
		g_autofree gchar *logical_id = NULL;
		logical_id =
		    g_path_get_basename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)));
		fu_device_set_logical_id(device, logical_id);
	}

	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "pci,drm_dp_aux_dev", error);
}

static FuFirmware *
fu_kinetic_dp_device_prepare_firmware(FuDevice *device,
				      GBytes *fw,
				      FwupdInstallFlags flags,
				      GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_kinetic_dp_firmware_new();

	/* parse input firmware file to two images */
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;

	return g_steal_pointer(&firmware);
}

static gboolean
fu_kinetic_dp_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuKineticDpDevice *self = FU_KINETIC_DP_DEVICE(device);
	FuKineticDpAuxIsp *aux_isp_ctrl = self->aux_isp_ctrl;

	g_return_val_if_fail(aux_isp_ctrl != NULL, FALSE);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 2);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98);

	/* get more information from control library */
	if (!fu_kinetic_dp_aux_isp_get_device_info(aux_isp_ctrl, self, DEV_HOST, error)) {
		g_prefix_error(error, "failed to read other device information: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* update firmware */
	if (!fu_kinetic_dp_aux_isp_start(aux_isp_ctrl, self, firmware, progress, error)) {
		g_prefix_error(error, "firmware update failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_kinetic_dp_device_rescan(FuDevice *device, GError **error)
{
	FuKineticDpDevice *self = FU_KINETIC_DP_DEVICE(device);
	g_autoptr(FuKineticDpConnection) connection = NULL;
	g_autofree gchar *name = NULL;
	g_autofree gchar *version = NULL;
	g_autofree gchar *guid = NULL;
	const gchar *chip_id;
	guint8 buf_ver[16];
	KtDpDevInfo *dp_dev_info = NULL;

	connection = fu_kinetic_dp_connection_new(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)));

	/* TODO: now only support to do ISP for Host chip */
	if (!fu_kinetic_dp_aux_isp_read_basic_device_info(self, DEV_HOST, &dp_dev_info, error)) {
		g_prefix_error(error, "failed to read device info: ");
		return FALSE;
	}

	g_debug("branch_id_str = %s", dp_dev_info->branch_id_str);

	/* set the corresponding AUX-ISP control library for the chip */
	fu_kinetic_dp_device_set_aux_isp_ctrl(self, dp_dev_info->chip_id);

	/* read firmware version */
	if (!fu_kinetic_dp_connection_read(
		connection,
		DPCD_ADDR_BRANCH_FW_MAJ_REV,
		buf_ver,
		DPCD_SIZE_BRANCH_FW_MAJ_REV + DPCD_SIZE_BRANCH_FW_MIN_REV + DPCD_SIZE_BRANCH_FW_REV,
		error))
		return FALSE;

	version = g_strdup_printf("%1d.%03d.%02d", buf_ver[0], buf_ver[1], buf_ver[2]);
	fu_device_set_version(FU_DEVICE(self), version);

	/* set up the device name */
	chip_id = fu_kinetic_dp_aux_isp_get_chip_id_str(dp_dev_info->chip_id);
	name = g_strdup_printf("%s", chip_id);
	fu_device_set_name(FU_DEVICE(self), name);

	/* detect chip family */
	self->family = fu_kinetic_dp_chip_id_to_family(dp_dev_info->chip_id);
	switch (self->family) {
	case FU_KINETIC_DP_FAMILY_JAGUAR:
		/* TODO: set max firmware size for Jaguar */
		break;
	case FU_KINETIC_DP_FAMILY_MUSTANG:
		/* TODO: determine max firmware size for Mustang */
		break;
	default:
		break;
	}

	/* add instand ID to generate GUIDs */
	guid = g_strdup_printf("KT-DP-%s", chip_id);
	fu_device_add_instance_id(FU_DEVICE(self), guid);

	/* add updatable flag if this device passed above check */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);

	return TRUE;
}

static void
fu_kinetic_dp_device_class_init(FuKineticDpDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	object_class->finalize = fu_kinetic_dp_device_finalize;

	klass_device->rescan = fu_kinetic_dp_device_rescan;
	klass_device->write_firmware = fu_kinetic_dp_device_write_firmware;
	klass_device->prepare_firmware = fu_kinetic_dp_device_prepare_firmware;
	klass_device->probe = fu_kinetic_dp_device_probe;
}

FuKineticDpDevice *
fu_kinetic_dp_device_new(FuUdevDevice *device)
{
	FuKineticDpDevice *self = g_object_new(FU_TYPE_KINETIC_DP_DEVICE, NULL);
	fu_device_incorporate(FU_DEVICE(self), FU_DEVICE(device));
	return self;
}
