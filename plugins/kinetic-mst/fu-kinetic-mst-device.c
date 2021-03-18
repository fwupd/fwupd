/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>

#include "fu-kinetic-mst-common.h"
#include "fu-kinetic-mst-device.h"

struct _FuKineticMstDevice {
    FuUdevDevice        parent_instance;
    gchar *             system_type;
    FuKineticMstFamily  family;
    FuKineticMstMode    mode;
};

G_DEFINE_TYPE (FuKineticMstDevice, fu_kinetic_mst_device, FU_TYPE_UDEV_DEVICE)

static void
fu_kinetic_mst_device_finalize (GObject *object)
{
	FuKineticMstDevice *self = FU_KINETIC_MST_DEVICE (object);

	g_free (self->system_type);

	G_OBJECT_CLASS (fu_kinetic_mst_device_parent_class)->finalize (object);
}

static void
fu_kinetic_mst_device_init (FuKineticMstDevice *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "com.kinetic.mst");
	fu_device_set_vendor (FU_DEVICE (self), "Kinetic");
	fu_device_add_vendor_id (FU_DEVICE (self), "DRM_DP_AUX_DEV:0x06CB");    // <TODO> How to determine the vendor ID?
	fu_device_set_summary (FU_DEVICE (self), "Multi-Stream Transport Device");
	fu_device_add_icon (FU_DEVICE (self), "video-display");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);  // <TODO> What's Kinetic's version format?
	fu_udev_device_set_flags (FU_UDEV_DEVICE (self),
                              FU_UDEV_DEVICE_FLAG_OPEN_READ |
                              FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
                              FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT);
}

static FuFirmware *
fu_kinetic_mst_device_prepare_firmware (FuDevice *device,
					  GBytes *fw,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE (device);
	g_autoptr(FuFirmware) firmware = fu_synaptics_mst_firmware_new ();

#if 0
	/* check firmware and board ID match */
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0 &&
	    !fu_device_has_custom_flag (device, "ignore-board-id")) {
		guint16 board_id = fu_synaptics_mst_firmware_get_board_id (FU_SYNAPTICS_MST_FIRMWARE (firmware));
		if (board_id != self->board_id) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "board ID mismatch, got 0x%04x, expected 0x%04x",
				     board_id, self->board_id);
			return NULL;
		}
	}
#else
    // <TODO> check firmware according to Kinetic's Fw image
#endif
	return fu_firmware_new_from_bytes (fw);
}

static gboolean
fu_kinetic_mst_device_write_firmware (FuDevice *device,
				        FuFirmware *firmware,
				        FwupdInstallFlags flags,
				        GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE (device);
	g_autoptr(GBytes) fw = NULL;
	const guint8 *payload_data;
	gsize payload_len;
	g_autoptr(FuDeviceLocker) locker = NULL;

	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;
	payload_data = g_bytes_get_data (fw, &payload_len);

#if 0
	/* enable remote control and disable on exit */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_SKIPS_RESTART)) {
		locker = fu_device_locker_new_full (self,
						(FuDeviceLockerFunc) fu_synaptics_mst_device_enable_rc,
						(FuDeviceLockerFunc) fu_synaptics_mst_device_restart,
						error);
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		fu_device_set_remove_delay (FU_DEVICE (self), 10000); /* a long time */
	} else {
		locker = fu_device_locker_new_full (self,
						(FuDeviceLockerFunc) fu_synaptics_mst_device_enable_rc,
						(FuDeviceLockerFunc) fu_synaptics_mst_device_disable_rc,
						error);
	}
	if (locker == NULL)
		return FALSE;

	/* update firmware */
	if (self->family == FU_SYNAPTICS_MST_FAMILY_PANAMERA) {
		if (!fu_synaptics_mst_device_panamera_prepare_write (self, error)) {
			g_prefix_error (error, "Failed to prepare for write: ");
			return FALSE;
		}
		if (!fu_synaptics_mst_device_update_esm (self,
							 payload_data,
							 error)) {
			g_prefix_error (error, "ESM update failed: ");
			return FALSE;
		}
		if (!fu_synaptics_mst_device_update_panamera_firmware (self,
								       payload_len,
								       payload_data,
								       error)) {
			g_prefix_error (error, "Firmware update failed: ");
			return FALSE;
		}
	} else {
		if (!fu_synaptics_mst_device_update_tesla_leaf_firmware (self,
									 payload_len,
									 payload_data,
									 error)) {
			g_prefix_error (error, "Firmware update failed: ");
			return FALSE;
		}
	}
#else
    // <TODO> implement
#endif

	/* wait for flash clear to settle */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_sleep_with_progress (device, 2);
	return TRUE;
}

FuKineticMstDevice *
fu_kinetic_mst_device_new (FuUdevDevice *device)
{
	FuKineticMstDevice *self = g_object_new (FU_TYPE_KINETIC_MST_DEVICE, NULL);
	fu_device_incorporate (FU_DEVICE (self), FU_DEVICE (device));
	return self;
}

void
fu_kinetic_mst_device_set_system_type (FuKineticMstDevice *self, const gchar *system_type)
{
	g_return_if_fail (FU_IS_KINETIC_MST_DEVICE (self));
	self->system_type = g_strdup (system_type);
}

static void
fu_kinetic_mst_device_class_init (FuKineticMstDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_kinetic_mst_device_finalize;
	//klass_device->to_string = fu_kinetic_mst_device_to_string;
	//klass_device->rescan = fu_kinetic_mst_device_rescan;
	klass_device->write_firmware = fu_kinetic_mst_device_write_firmware;
	klass_device->prepare_firmware = fu_kinetic_mst_device_prepare_firmware;
	//klass_device->probe = fu_kinetic_mst_device_probe;
}

