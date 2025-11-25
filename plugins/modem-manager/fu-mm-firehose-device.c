/*
 * Copyright 2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright 2021 Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-firehose-device.h"

struct _FuMmFirehoseDevice {
	FuMmDevice parent_instance;
};

G_DEFINE_TYPE(FuMmFirehoseDevice, fu_mm_firehose_device, FU_TYPE_MM_DEVICE)

static gboolean
fu_mm_firehose_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMmFirehoseDevice *self = FU_MM_FIREHOSE_DEVICE(device);

	if (!fu_mm_device_at_cmd(FU_MM_DEVICE(self), "AT", TRUE, error))
		return FALSE;
	if (!fu_mm_device_at_cmd(FU_MM_DEVICE(self), "AT^SFIREHOSE", TRUE, error)) {
		g_prefix_error_literal(error, "enabling firmware download mode not supported: ");
		return FALSE;
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mm_firehose_device_probe(FuDevice *device, GError **error)
{
	FuMmFirehoseDevice *self = FU_MM_FIREHOSE_DEVICE(device);
	return fu_mm_device_set_device_file(FU_MM_DEVICE(self), MM_MODEM_PORT_TYPE_AT, error);
}

static gboolean
fu_mm_firehose_device_prepare(FuDevice *device,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuMmFirehoseDevice *self = FU_MM_FIREHOSE_DEVICE(device);
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), TRUE);
	return TRUE;
}

static gboolean
fu_mm_firehose_device_cleanup(FuDevice *device,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuMmFirehoseDevice *self = FU_MM_FIREHOSE_DEVICE(device);
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), FALSE);
	return TRUE;
}

static void
fu_mm_firehose_device_set_progress(FuDevice *self, FuProgress *progress)
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
fu_mm_firehose_device_init(FuMmFirehoseDevice *self)
{
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_add_protocol(FU_UDEV_DEVICE(self), "com.qualcomm.firehose");
}

static void
fu_mm_firehose_device_class_init(FuMmFirehoseDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_mm_firehose_device_probe;
	device_class->detach = fu_mm_firehose_device_detach;
	device_class->prepare = fu_mm_firehose_device_prepare;
	device_class->cleanup = fu_mm_firehose_device_cleanup;
	device_class->set_progress = fu_mm_firehose_device_set_progress;
}
