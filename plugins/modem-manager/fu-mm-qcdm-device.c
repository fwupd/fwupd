/*
 * Copyright 2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright 2021 Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-qcdm-device.h"

G_DEFINE_TYPE(FuMmQcdmDevice, fu_mm_qcdm_device, FU_TYPE_MM_DEVICE)

static gboolean
fu_mm_qcdm_device_cmd(FuMmQcdmDevice *self, const guint8 *buf, gsize bufsz, GError **error)
{
	g_autoptr(GBytes) qcdm_req = NULL;
	g_autoptr(GBytes) qcdm_res = NULL;

	/* command */
	qcdm_req = g_bytes_new(buf, bufsz);
	fu_dump_bytes(G_LOG_DOMAIN, "writing", qcdm_req);
	if (!fu_udev_device_write_bytes(FU_UDEV_DEVICE(self),
					qcdm_req,
					1500,
					FU_IO_CHANNEL_FLAG_FLUSH_INPUT,
					error)) {
		g_prefix_error(error, "failed to write qcdm command: ");
		return FALSE;
	}

	/* response */
	qcdm_res = fu_udev_device_read_bytes(FU_UDEV_DEVICE(self),
					     bufsz,
					     1500,
					     FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					     error);
	if (qcdm_res == NULL) {
		g_prefix_error(error, "failed to read qcdm response: ");
		return FALSE;
	}
	fu_dump_bytes(G_LOG_DOMAIN, "read", qcdm_res);

	/* command == response */
	if (g_bytes_compare(qcdm_res, qcdm_req) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to read valid qcdm response");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_qcdm_device_switch_to_edl_cb(FuDevice *device, gpointer userdata, GError **error)
{
	FuMmQcdmDevice *self = FU_MM_QCDM_DEVICE(device);
	const guint8 buf[] = {0x4B, 0x65, 0x01, 0x00, 0x54, 0x0F, 0x7E};

	/* when the QCDM port does not exist anymore, we are detached */
	if (!g_file_test(fu_udev_device_get_device_file(FU_UDEV_DEVICE(self)), G_FILE_TEST_EXISTS))
		return TRUE;
	return fu_mm_qcdm_device_cmd(self, buf, sizeof(buf), error);
}

static gboolean
fu_mm_qcdm_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	/* retry up to 30 times until the QCDM port goes away */
	if (!fu_device_retry_full(device,
				  fu_mm_qcdm_device_switch_to_edl_cb,
				  30,
				  1000,
				  NULL,
				  error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mm_qcdm_device_probe(FuDevice *device, GError **error)
{
	FuMmQcdmDevice *self = FU_MM_QCDM_DEVICE(device);
	return fu_mm_device_set_device_file(FU_MM_DEVICE(self), MM_MODEM_PORT_TYPE_QCDM, error);
}

static gboolean
fu_mm_qcdm_device_prepare(FuDevice *device,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuMmQcdmDevice *self = FU_MM_QCDM_DEVICE(device);
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), TRUE);
	return TRUE;
}

static gboolean
fu_mm_qcdm_device_cleanup(FuDevice *device,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuMmQcdmDevice *self = FU_MM_QCDM_DEVICE(device);
	fu_mm_device_set_inhibited(FU_MM_DEVICE(self), FALSE);
	return TRUE;
}

static void
fu_mm_qcdm_device_set_progress(FuDevice *self, FuProgress *progress)
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
fu_mm_qcdm_device_init(FuMmQcdmDevice *self)
{
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_add_instance_id_full(FU_DEVICE(self),
				       "USB\\VID_05C6&PID_9008",
				       FU_DEVICE_INSTANCE_FLAG_COUNTERPART);
	fu_device_add_protocol(FU_UDEV_DEVICE(self), "com.qualcomm.firehose");
}

static void
fu_mm_qcdm_device_class_init(FuMmQcdmDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_mm_qcdm_device_probe;
	device_class->detach = fu_mm_qcdm_device_detach;
	device_class->prepare = fu_mm_qcdm_device_prepare;
	device_class->cleanup = fu_mm_qcdm_device_cleanup;
	device_class->set_progress = fu_mm_qcdm_device_set_progress;
}
