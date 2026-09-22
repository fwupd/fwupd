/*
 * Copyright 2026 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-amd-gpu-pldm-device.h"
#include "fu-amd-gpu-pldm-firmware.h"

struct _FuAmdGpuPldmDevice {
	FuUdevDevice parent_instance;
};

/**
 * FuAmdGpuPldmDevice:
 *
 * The PLDM remote-management firmware of an AMD accelerator or dGPU.
 *
 * Some devices expose a Remote Management chip that orchestrates flashing the
 * firmware images of all applicable devices. It is driven via the
 * `remote_mgmt_fw` / `remote_mgmt_fw_status` sysfs interface documented by the
 * amdgpu driver, and consumes a PLDM firmware update package (DMTF DSP0267).
 *
 * This device shares the sysfs backing of the #FuAmdGpuDevice it manages and is
 * exposed as its parent.
 */

/* mirrors the psp_vbflash status semantics; the remote_mgmt_fw kernel interface
 * is not yet merged, so adjust these once the driver lands */
#define REMOTE_MGMT_MAX_POLL 1500
#define REMOTE_MGMT_SUCCESS  0x80000000

G_DEFINE_TYPE(FuAmdGpuPldmDevice, fu_amd_gpu_pldm_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_amd_gpu_pldm_device_setup(FuDevice *device, GError **error)
{
	guint64 vid = 0;
	guint64 pid = 0;
	g_autofree gchar *version = NULL;
	g_autofree gchar *vendor = NULL;
	g_autofree gchar *model = NULL;

	/* current firmware version */
	version = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					    "pldm_fw_version",
					    FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					    error);
	if (version == NULL) {
		g_prefix_error_literal(error, "failed to read pldm_fw_version: ");
		return FALSE;
	}
	fu_device_set_version(device, version);

	/* generate a GUID that is distinct from the managed device's PCI GUIDs */
	vendor = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					   "vendor",
					   FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					   error);
	if (vendor == NULL)
		return FALSE;
	model = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					  "device",
					  FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					  error);
	if (model == NULL)
		return FALSE;
	if (!fu_strtoull(vendor, &vid, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
		return FALSE;
	if (!fu_strtoull(model, &pid, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
		return FALSE;
	fu_device_add_instance_u16(device, "VEN", vid);
	fu_device_add_instance_u16(device, "DEV", pid);
	if (!fu_device_build_instance_id(device, error, "AMDPLDM", "VEN", "DEV", NULL))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_amd_gpu_pldm_device_wait_for_completion_cb(FuDevice *device, gpointer user_data, GError **error)
{
	guint64 status = 0;
	g_autofree gchar *buf = NULL;

	buf = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					"remote_mgmt_fw_status",
					FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					error);
	if (buf == NULL)
		return FALSE;
	if (!fu_strtoull(buf, &status, 0, G_MAXUINT64, FU_INTEGER_BASE_AUTO, error))
		return FALSE;
	if (status != REMOTE_MGMT_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "status was %" G_GUINT64_FORMAT,
			    status);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_amd_gpu_pldm_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	const gchar *base;
	g_autofree gchar *remote_mgmt_fw = NULL;
	g_autoptr(FuIOChannel) image_io = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_read = NULL;

	/* emulation doesn't currently cover IO channel use */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	base = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	remote_mgmt_fw = g_build_filename(base, "remote_mgmt_fw", NULL);

	image_io =
	    fu_io_channel_new_file(remote_mgmt_fw,
				   FU_IO_CHANNEL_OPEN_FLAG_READ | FU_IO_CHANNEL_OPEN_FLAG_WRITE,
				   error);
	if (image_io == NULL)
		return FALSE;

	fu_progress_set_id(progress, G_STRLOC);

	/* stage the bundle */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	if (!fu_io_channel_write_bytes(image_io, fw, 100, FU_IO_CHANNEL_FLAG_NONE, error))
		return FALSE;

	/* trigger the update (this looks funny but amdgpu returns 0 bytes) */
	if (!fu_io_channel_read_raw(image_io,
				    NULL,
				    1,
				    NULL,
				    100,
				    FU_IO_CHANNEL_FLAG_NONE,
				    &error_read))
		g_debug("triggered update: %s", error_read->message);

	/* poll for completion */
	return fu_device_retry_full(device,
				    fu_amd_gpu_pldm_device_wait_for_completion_cb,
				    REMOTE_MGMT_MAX_POLL,
				    100, /* ms */
				    NULL,
				    error);
}

static void
fu_amd_gpu_pldm_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, NULL); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, NULL); /* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, NULL); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, NULL);    /* reload */
}

static void
fu_amd_gpu_pldm_device_init(FuAmdGpuPldmDevice *self)
{
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_AMD_GPU_PLDM_FIRMWARE);
	fu_device_set_logical_id(FU_DEVICE(self), "pldm");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_GUIDS);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_set_install_duration(FU_DEVICE(self), 70);
	fu_device_add_protocol(FU_DEVICE(self), "com.amd.pldm");
	fu_device_add_icon(FU_DEVICE(self), "video-display");
}

static void
fu_amd_gpu_pldm_device_class_init(FuAmdGpuPldmDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_amd_gpu_pldm_device_setup;
	device_class->write_firmware = fu_amd_gpu_pldm_device_write_firmware;
	device_class->set_progress = fu_amd_gpu_pldm_device_set_progress;
}

FuDevice *
fu_amd_gpu_pldm_device_new(FuDevice *donor)
{
	g_autoptr(FuAmdGpuPldmDevice) self = NULL;

	g_return_val_if_fail(FU_IS_DEVICE(donor), NULL);

	self = g_object_new(FU_TYPE_AMD_GPU_PLDM_DEVICE,
			    "context",
			    fu_device_get_context(donor),
			    NULL);

	/* share the managed device's sysfs backing */
	fu_device_incorporate(FU_DEVICE(self),
			      donor,
			      FU_DEVICE_INCORPORATE_FLAG_BACKEND_ID |
				  FU_DEVICE_INCORPORATE_FLAG_VENDOR |
				  FU_DEVICE_INCORPORATE_FLAG_EVENTS);
	if (fu_device_get_name(donor) != NULL) {
		g_autofree gchar *name =
		    g_strdup_printf("%s Remote Management", fu_device_get_name(donor));
		fu_device_set_name(FU_DEVICE(self), name);
	}

	return FU_DEVICE(g_steal_pointer(&self));
}
