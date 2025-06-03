/*
 * Copyright 2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright 2021 Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-mhi-qcdm-device.h"

struct _FuMmMhiQcdmDevice {
	FuMmQcdmDevice parent_instance;
	FuKernelSearchPathLocker *search_path_locker;
	gchar *firehose_prog_file;
};

G_DEFINE_TYPE(FuMmMhiQcdmDevice, fu_mm_mhi_qcdm_device, FU_TYPE_MM_QCDM_DEVICE)

static gboolean
fu_mm_mhi_qcdm_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMmMhiQcdmDevice *self = FU_MM_MHI_QCDM_DEVICE(device);

	/* sanity check */
	if (self->firehose_prog_file == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "Firehose prog filename is not set for the device");
		return FALSE;
	}

	/* override the baseclass to do nothing; we're handling this in ->write_firmware() */
	return TRUE;
}

static FuKernelSearchPathLocker *
fu_mm_mhi_qcdm_device_search_path_locker_new(FuMmMhiQcdmDevice *self, GError **error)
{
	g_autofree gchar *cachedir = NULL;
	g_autofree gchar *mm_fw_dir = NULL;
	g_autoptr(FuKernelSearchPathLocker) locker = NULL;

	/* create a directory to store firmware files for modem-manager plugin */
	cachedir = fu_path_from_kind(FU_PATH_KIND_CACHEDIR_PKG);
	mm_fw_dir = g_build_filename(cachedir, "modem-manager", "firmware", NULL);
	if (g_mkdir_with_parents(mm_fw_dir, 0700) == -1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to create '%s': %s",
			    mm_fw_dir,
			    fwupd_strerror(errno));
		return NULL;
	}
	locker = fu_kernel_search_path_locker_new(mm_fw_dir, error);
	if (locker == NULL)
		return NULL;
	return g_steal_pointer(&locker);
}

static gboolean
fu_mm_mhi_qcdm_device_prepare(FuDevice *device,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuMmMhiQcdmDevice *self = FU_MM_MHI_QCDM_DEVICE(device);

	/* in the case of MHI PCI modems, the mhi-pci-generic driver reads the firehose binary
	 * from the firmware-loader and writes it to the modem */
	self->search_path_locker = fu_mm_mhi_qcdm_device_search_path_locker_new(self, error);
	if (self->search_path_locker == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_mm_mhi_qcdm_device_cleanup(FuDevice *device,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuMmMhiQcdmDevice *self = FU_MM_MHI_QCDM_DEVICE(device);

	/* restore the firmware search path */
	g_clear_object(&self->search_path_locker);

	/* success */
	return TRUE;
}

static gboolean
fu_mm_mhi_qcdm_device_copy_firehose_prog(FuMmMhiQcdmDevice *self, GBytes *prog, GError **error)
{
	const gchar *path = fu_kernel_search_path_locker_get_path(self->search_path_locker);
	g_autofree gchar *fn = g_build_filename(path, "qcom", self->firehose_prog_file, NULL);
	if (!fu_path_mkdir_parent(fn, error))
		return FALSE;
	return fu_bytes_set_contents(fn, prog, error);
}

static gboolean
fu_mm_mhi_qcdm_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuMmMhiQcdmDevice *self = FU_MM_MHI_QCDM_DEVICE(device);
	g_autoptr(GBytes) firehose_prog = NULL;

	/* firehose modems that use mhi_pci drivers require firehose binary
	 * to be present in the firmware-loader search path. */
	firehose_prog = fu_firmware_get_image_by_id_bytes(firmware, "firehose-prog.mbn", error);
	if (firehose_prog == NULL)
		return FALSE;
	if (!fu_mm_mhi_qcdm_device_copy_firehose_prog(self, firehose_prog, error))
		return FALSE;

	/* trigger emergency download mode; this takes us to the EDL execution environment */
	if (!FU_DEVICE_CLASS(fu_mm_mhi_qcdm_device_parent_class)->detach(device, progress, error))
		return FALSE;

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
	return TRUE;
}

static gboolean
fu_mm_mhi_qcdm_device_set_quirk_kv(FuDevice *device,
				   const gchar *key,
				   const gchar *value,
				   GError **error)
{
	FuMmMhiQcdmDevice *self = FU_MM_MHI_QCDM_DEVICE(device);

	if (g_strcmp0(key, "ModemManagerFirehoseProgFile") == 0) {
		self->firehose_prog_file = g_strdup(value);
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_mm_mhi_qcdm_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_mm_mhi_qcdm_device_init(FuMmMhiQcdmDevice *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), 5000);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_add_protocol(FU_UDEV_DEVICE(self), "com.qualcomm.firehose");
}

static void
fu_mm_mhi_qcdm_device_finalize(GObject *object)
{
	FuMmMhiQcdmDevice *self = FU_MM_MHI_QCDM_DEVICE(object);
	g_free(self->firehose_prog_file);
	G_OBJECT_CLASS(fu_mm_mhi_qcdm_device_parent_class)->finalize(object);
}

static void
fu_mm_mhi_qcdm_device_class_init(FuMmMhiQcdmDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_mm_mhi_qcdm_device_finalize;
	device_class->detach = fu_mm_mhi_qcdm_device_detach;
	device_class->prepare = fu_mm_mhi_qcdm_device_prepare;
	device_class->cleanup = fu_mm_mhi_qcdm_device_cleanup;
	device_class->write_firmware = fu_mm_mhi_qcdm_device_write_firmware;
	device_class->set_quirk_kv = fu_mm_mhi_qcdm_device_set_quirk_kv;
	device_class->set_progress = fu_mm_mhi_qcdm_device_set_progress;
}
