/*
 * Copyright 2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright 2021 Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"
#include "fu-mm-mhi-firehose-device.h"

struct _FuMmMhiFirehoseDevice {
	FuMmDevice parent_instance;
	FuKernelSearchPathLocker *search_path_locker;
	GBytes *firehose_prog;
	gchar *firehose_prog_file;
	gchar *lib_firmware_path;
};

G_DEFINE_TYPE(FuMmMhiFirehoseDevice, fu_mm_mhi_firehose_device, FU_TYPE_MM_DEVICE)

static gboolean
fu_mm_mhi_firehose_device_trigger_edl(FuDevice *device, GError **error)
{
	const gchar *mhi_name = NULL;
	g_autoptr(GPtrArray) attrs = NULL;
	g_autofree gchar *trigger_path = NULL;

	attrs = fu_udev_device_list_sysfs(FU_UDEV_DEVICE(device), error);
	if (attrs == NULL) {
		g_prefix_error(error, "failed to list sysfs entries: ");
		return FALSE;
	}

	/* Find the first MHI port (only one expected) */
	for (guint i = 0; i < attrs->len; i++) {
		const gchar *name = g_ptr_array_index(attrs, i);
		if (g_str_has_prefix(name, "mhi")) {
			mhi_name = name;
			break;
		}
	}

	if (mhi_name == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
				    "No MHI port found");
		return FALSE;
	}

	trigger_path = g_strdup_printf("%s/trigger_edl", mhi_name);
	if (fu_udev_device_write_sysfs(FU_UDEV_DEVICE(device), trigger_path, "1", 3000, error))
		return TRUE;

	/* write_sysfs already set a detailed error, just add context */
	g_prefix_error(error, "failed to trigger EDL for MHI port '%s': ", mhi_name);
	return FALSE;
}

static gboolean
fu_mm_mhi_firehose_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	const gchar *path;
	g_autofree gchar *fn = NULL;
	FuMmMhiFirehoseDevice *self = FU_MM_MHI_FIREHOSE_DEVICE(device);

	/* sanity check */
	if (self->search_path_locker == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "search path locker not set");
		return FALSE;
	}
	if (self->firehose_prog_file == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "Firehose prog filename is not set for the device");
		return FALSE;
	}

	/* copy the firehose prog into the search path locker */
	path = fu_kernel_search_path_locker_get_path(self->search_path_locker);
	fn = g_build_filename(path, self->firehose_prog_file, NULL);
	if (!fu_bytes_set_contents(fn, self->firehose_prog, error)) {
		g_prefix_error_literal(error, "create firehose prog file fail");
		return FALSE;
	}

	/* trigger emergency download mode; this takes us to the EDL execution environment */
	if (!fu_mm_mhi_firehose_device_trigger_edl(device, error)) {
		g_prefix_error_literal(error, "rebooting into firehose port not supported: ");
		return FALSE;
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static FuKernelSearchPathLocker *
fu_mm_mhi_firehose_device_search_path_locker_new(FuMmMhiFirehoseDevice *self, GError **error)
{
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	FuPathStore *pstore = fu_context_get_path_store(ctx);
	g_autofree gchar *mm_fw_dir = NULL;
	g_autoptr(FuKernelSearchPathLocker) locker = NULL;

	mm_fw_dir = g_build_filename(self->lib_firmware_path, NULL);
	if (mm_fw_dir == NULL)
		return NULL;
	if (g_mkdir_with_parents(mm_fw_dir, 0777) == -1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to create '%s': %s",
			    mm_fw_dir,
			    fwupd_strerror(errno));
		return NULL;
	}
	locker = fu_kernel_search_path_locker_new(pstore, mm_fw_dir, error);
	if (locker == NULL)
		return NULL;
	return g_steal_pointer(&locker);
}

static gboolean
fu_mm_mhi_firehose_device_prepare(FuDevice *device,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuMmMhiFirehoseDevice *self = FU_MM_MHI_FIREHOSE_DEVICE(device);

	/* in the case of MHI PCI modems, the mhi-pci-generic driver reads the firehose binary
	 * from the firmware-loader and writes it to the modem */
	self->search_path_locker = fu_mm_mhi_firehose_device_search_path_locker_new(self, error);
	if (self->search_path_locker == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_mm_mhi_firehose_device_cleanup(FuDevice *device,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuMmMhiFirehoseDevice *self = FU_MM_MHI_FIREHOSE_DEVICE(device);

	/* restore the firmware search path */
	g_clear_object(&self->search_path_locker);

	/* no longer required */
	g_clear_pointer(&self->firehose_prog, g_bytes_unref);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_mm_mhi_firehose_device_prepare_firmware(FuDevice *device,
				       GInputStream *stream,
				       FuProgress *progress,
				       FuFirmwareParseFlags flags,
				       GError **error)
{
	FuMmMhiFirehoseDevice *self = FU_MM_MHI_FIREHOSE_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_zip_firmware_new();

	/* parse as archive */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* firehose modems that use mhi_pci drivers require firehose binary
	 * to be present in the firmware-loader search path. */
	self->firehose_prog =
	    fu_firmware_get_image_by_id_bytes(firmware,
					      "firehose-prog.mbn|prog_nand*.mbn|prog_firehose*",
					      error);
	if (self->firehose_prog == NULL)
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_mm_mhi_firehose_device_set_quirk_kv(FuDevice *device,
				   const gchar *key,
				   const gchar *value,
				   GError **error)
{
	FuMmMhiFirehoseDevice *self = FU_MM_MHI_FIREHOSE_DEVICE(device);

	if (g_strcmp0(key, "ModemManagerFirehoseProgFile") == 0) {
		self->firehose_prog_file = g_strdup(value);
		return TRUE;
	}

	if (g_strcmp0(key, "ModemManagerLibFirmwarePath") == 0) {
		self->lib_firmware_path = g_strdup(value);
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
fu_mm_mhi_firehose_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static gboolean
fu_mm_mhi_firehose_device_probe(FuDevice *device, GError **error)
{
    const gchar *sysfs_path;
    g_autoptr(GPtrArray) attrs = NULL;
    const gchar *mhi_name = NULL;
    g_autofree gchar *devnode = NULL;  

    sysfs_path = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
    if (sysfs_path == NULL) {
        g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
                            "failed to get sysfs path");
        return FALSE;
    }

    attrs = fu_udev_device_list_sysfs(FU_UDEV_DEVICE(device), error);
    if (attrs == NULL) {
        g_prefix_error(error, "failed to list sysfs entries: ");
        return FALSE;
    }

    for (guint i = 0; i < attrs->len; i++) {
        const gchar *name = g_ptr_array_index(attrs, i);
        if (g_str_has_prefix(name, "mhi")) {
            mhi_name = name;
            break;
        }
    }

    if (mhi_name == NULL) {
        g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
                            "No MHI device found");
        return FALSE;
    }

    devnode = g_strdup_printf("%s/%s/%s_MBIM", sysfs_path, mhi_name, mhi_name);

    if (!g_file_test(devnode, G_FILE_TEST_EXISTS)) {
        g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
                            "No MBIM port found");
        return FALSE;
    }

    g_info("found MBIM port at %s", devnode);
    return TRUE;
}

static void
fu_mm_mhi_firehose_device_init(FuMmMhiFirehoseDevice *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), 30000);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_add_protocol(FU_UDEV_DEVICE(self), "com.qualcomm.firehose");
}

static void
fu_mm_mhi_firehose_device_finalize(GObject *object)
{
	FuMmMhiFirehoseDevice *self = FU_MM_MHI_FIREHOSE_DEVICE(object);
	g_free(self->firehose_prog_file);
	g_free(self->lib_firmware_path);
	if (self->firehose_prog != NULL)
		g_bytes_unref(self->firehose_prog);
	G_OBJECT_CLASS(fu_mm_mhi_firehose_device_parent_class)->finalize(object);
}

static void
fu_mm_mhi_firehose_device_class_init(FuMmMhiFirehoseDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_mm_mhi_firehose_device_finalize;
	device_class->probe = fu_mm_mhi_firehose_device_probe;
	device_class->detach = fu_mm_mhi_firehose_device_detach;
	device_class->prepare = fu_mm_mhi_firehose_device_prepare;
	device_class->cleanup = fu_mm_mhi_firehose_device_cleanup;
	device_class->prepare_firmware = fu_mm_mhi_firehose_device_prepare_firmware;
	device_class->set_quirk_kv = fu_mm_mhi_firehose_device_set_quirk_kv;
	device_class->set_progress = fu_mm_mhi_firehose_device_set_progress;
}
