/*
 * Copyright 2023 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <fcntl.h>
#include <glib/gstdio.h>
#include <libdrm/amdgpu.h>
#include <libdrm/amdgpu_drm.h>

#include "fu-amd-gpu-atom-firmware.h"
#include "fu-amd-gpu-device.h"
#include "fu-amd-gpu-psp-firmware.h"

struct _FuAmdGpuDevice {
	FuPciDevice parent_instance;
	gchar *vbios_pn;
	guint32 drm_major;
	guint32 drm_minor;
};

#define PSPVBFLASH_MAX_POLL    1500
#define PSPVBFLASH_NOT_STARTED 0x0
#define PSPVBFLASH_IN_PROGRESS 0x1
#define PSPVBFLASH_SUCCESS     0x80000000

#define PART_NUM_STR_SIZE 10

G_DEFINE_TYPE(FuAmdGpuDevice, fu_amd_gpu_device, FU_TYPE_PCI_DEVICE)

static void
fu_amd_gpu_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuAmdGpuDevice *self = FU_AMDGPU_DEVICE(device);

	fwupd_codec_string_append_int(str, idt, "DrmMajor", self->drm_major);
	fwupd_codec_string_append_int(str, idt, "DrmMinor", self->drm_minor);
}

static gboolean
fu_amd_gpu_device_set_device_file(FuDevice *device, const gchar *base, GError **error)
{
	const gchar *f;
	g_autofree gchar *ddir = NULL;
	g_autoptr(GDir) dir = NULL;

	ddir = g_build_filename(base, "drm", NULL);
	dir = g_dir_open(ddir, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((f = g_dir_read_name(dir))) {
		if (g_str_has_prefix(f, "card")) {
			g_autofree gchar *devbase = NULL;
			g_autofree gchar *device_file = NULL;

			devbase = fu_path_from_kind(FU_PATH_KIND_DEVFS);
			device_file = g_build_filename(devbase, "dri", f, NULL);
			fu_udev_device_set_device_file(FU_UDEV_DEVICE(device), device_file);
			return TRUE;
		}
	}

	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no DRM device file found");
	return FALSE;
}

static gboolean
fu_amd_gpu_device_probe(FuDevice *device, GError **error)
{
	const gchar *base;
	g_autofree gchar *rom = NULL;
	g_autofree gchar *psp_vbflash = NULL;
	g_autofree gchar *psp_vbflash_status = NULL;

	base = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	if (!fu_amd_gpu_device_set_device_file(device, base, error))
		return FALSE;

	/* APUs don't have 'rom' sysfs file */
	rom = g_build_filename(base, "rom", NULL);
	if (!g_file_test(rom, G_FILE_TEST_EXISTS)) {
		fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_HOST_CPU_CHILD);
		fu_udev_device_add_open_flag(FU_UDEV_DEVICE(device), FU_IO_CHANNEL_OPEN_FLAG_READ);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	} else {
		fu_device_set_logical_id(device, "rom");
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
		fu_udev_device_add_open_flag(FU_UDEV_DEVICE(device), FU_IO_CHANNEL_OPEN_FLAG_READ);
	}

	/* firmware upgrade support */
	psp_vbflash = g_build_filename(base, "psp_vbflash", NULL);
	psp_vbflash_status = g_build_filename(base, "psp_vbflash_status", NULL);
	if (g_file_test(psp_vbflash, G_FILE_TEST_EXISTS) &&
	    g_file_test(psp_vbflash_status, G_FILE_TEST_EXISTS)) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SELF_RECOVERY);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
		fu_device_set_install_duration(device, 70);
		fu_device_add_protocol(device, "com.amd.pspvbflash");
	}

	return TRUE;
}

static void
fu_amd_gpu_device_set_marketing_name(FuDevice *device)
{
	FuIOChannel *io_channel = fu_udev_device_get_io_channel(FU_UDEV_DEVICE(device));
	FuAmdGpuDevice *self = FU_AMDGPU_DEVICE(device);
	amdgpu_device_handle device_handle = {0};
	gint r;

	r = amdgpu_device_initialize(fu_io_channel_unix_get_fd(io_channel),
				     &self->drm_major,
				     &self->drm_minor,
				     &device_handle);
	if (r == 0) {
		const gchar *marketing_name = amdgpu_get_marketing_name(device_handle);
		if (marketing_name != NULL)
			fu_device_set_name(device, marketing_name);
	} else
		g_warning("unable to set marketing name: %s", g_strerror(r));
}

static gboolean
fu_amd_gpu_device_setup(FuDevice *device, GError **error)
{
	FuAmdGpuDevice *self = FU_AMDGPU_DEVICE(device);
	struct drm_amdgpu_info_vbios vbios_info;
	struct drm_amdgpu_info request = {
	    .query = AMDGPU_INFO_VBIOS,
	    .return_pointer = GPOINTER_TO_SIZE(&vbios_info),
	    .return_size = sizeof(struct drm_amdgpu_info_vbios),
	    .vbios_info.type = AMDGPU_INFO_VBIOS_INFO,
	};
	g_autofree gchar *part = NULL;
	g_autofree gchar *ver = NULL;
	g_autofree gchar *model = NULL;

	fu_amd_gpu_device_set_marketing_name(device);

	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(device),
				  DRM_IOCTL_AMDGPU_INFO,
				  (void *)&request,
				  sizeof(request),
				  NULL,
				  1000,
				  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
				  error))
		return FALSE;
	self->vbios_pn = fu_strsafe((const gchar *)vbios_info.vbios_pn, PART_NUM_STR_SIZE);
	part = g_strdup_printf("AMD\\%s", self->vbios_pn);
	fu_device_add_instance_id(device, part);
	fu_device_set_version_raw(device, vbios_info.version);
	ver = fu_strsafe((const gchar *)vbios_info.vbios_ver_str, sizeof(vbios_info.vbios_ver_str));
	fu_device_set_version(device, ver); /* nocheck:set-version */
	model = fu_strsafe((const gchar *)vbios_info.name, sizeof(vbios_info.name));
	fu_device_set_summary(device, model);

	return TRUE;
}

static FuFirmware *
fu_amd_gpu_device_prepare_firmware(FuDevice *device,
				   GInputStream *stream,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuAmdGpuDevice *self = FU_AMDGPU_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_amd_gpu_psp_firmware_new();
	g_autoptr(FuFirmware) ish_a = NULL;
	g_autoptr(FuFirmware) partition_a = NULL;
	g_autoptr(FuFirmware) csm = NULL;
	g_autofree gchar *fw_pn = NULL;

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* we will always flash the contents of partition A */
	ish_a = fu_firmware_get_image_by_id(firmware, "ISH_A", error);
	if (ish_a == NULL)
		return NULL;
	partition_a = fu_firmware_get_image_by_id(ish_a, "PARTITION_A", error);
	if (partition_a == NULL)
		return NULL;
	csm = fu_firmware_get_image_by_id(partition_a, "ATOM_CSM_A", error);
	if (csm == NULL)
		return NULL;

	fw_pn = fu_strsafe(fu_amd_gpu_atom_firmware_get_vbios_pn(csm), PART_NUM_STR_SIZE);
	if (g_strcmp0(fw_pn, self->vbios_pn) != 0) {
		if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "firmware for %s does not match %s",
				    fw_pn,
				    self->vbios_pn);
			return NULL;
		}
		g_warning("firmware for %s does not match %s but is being force installed anyway",
			  fw_pn,
			  self->vbios_pn);
	}

	return g_steal_pointer(&firmware);
}

static gboolean
fu_amd_gpu_device_wait_for_completion_cb(FuDevice *device, gpointer user_data, GError **error)
{
	const gchar *base = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	gsize sz = 0;
	guint64 status = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *psp_vbflash_status = NULL;

	psp_vbflash_status = g_build_filename(base, "psp_vbflash_status", NULL);
	if (!g_file_get_contents(psp_vbflash_status, &buf, &sz, error))
		return FALSE;
	if (!fu_strtoull(buf, &status, 0, G_MAXUINT64, FU_INTEGER_BASE_AUTO, error))
		return FALSE;
	if (status != PSPVBFLASH_SUCCESS) {
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
fu_amd_gpu_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	g_autofree gchar *psp_vbflash = NULL;
	g_autoptr(FuIOChannel) image_io = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_read = NULL;
	const gchar *base;

	base = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	psp_vbflash = g_build_filename(base, "psp_vbflash", NULL);

	image_io =
	    fu_io_channel_new_file(psp_vbflash,
				   FU_IO_CHANNEL_OPEN_FLAG_READ | FU_IO_CHANNEL_OPEN_FLAG_WRITE,
				   error);
	if (image_io == NULL)
		return FALSE;

	fu_progress_set_id(progress, G_STRLOC);

	/* stage the image */
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
				    fu_amd_gpu_device_wait_for_completion_cb,
				    PSPVBFLASH_MAX_POLL,
				    100, /* ms */
				    NULL,
				    error);
}

static void
fu_amd_gpu_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, NULL); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, NULL); /* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, NULL); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, NULL);    /* reload */
}

static void
fu_amd_gpu_device_init(FuAmdGpuDevice *self)
{
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_AUTO_PARENT_CHILDREN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_GUIDS);
}

static void
fu_amd_gpu_device_finalize(GObject *object)
{
	FuAmdGpuDevice *self = FU_AMDGPU_DEVICE(object);
	g_free(self->vbios_pn);
	G_OBJECT_CLASS(fu_amd_gpu_device_parent_class)->finalize(object);
}

static void
fu_amd_gpu_device_class_init(FuAmdGpuDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_amd_gpu_device_finalize;
	device_class->probe = fu_amd_gpu_device_probe;
	device_class->setup = fu_amd_gpu_device_setup;
	device_class->set_progress = fu_amd_gpu_device_set_progress;
	device_class->write_firmware = fu_amd_gpu_device_write_firmware;
	device_class->prepare_firmware = fu_amd_gpu_device_prepare_firmware;
	device_class->to_string = fu_amd_gpu_device_to_string;
}
