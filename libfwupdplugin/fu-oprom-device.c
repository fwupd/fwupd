/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-device-locker.h"
#include "fu-oprom-device.h"
#include "fu-output-stream.h"

G_DEFINE_TYPE(FuOpromDevice, fu_oprom_device, FU_TYPE_PCI_DEVICE)

static gboolean
fu_oprom_device_probe(FuDevice *device, GError **error)
{
	FuOpromDevice *self = FU_OPROM_DEVICE(device);
	gboolean rom_exists = FALSE;
	g_autofree gchar *rom_fn = NULL;

	/* does the device even have ROM? */
	rom_fn = g_build_filename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self)), "rom", NULL);
	if (!fu_device_query_file_exists(device, rom_fn, &rom_exists, error))
		return FALSE;
	if (rom_exists)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	return TRUE;
}

static gboolean
fu_oprom_device_set_enabled(FuOpromDevice *self, gboolean value, GError **error)
{
	g_autofree gchar *rom_fn = NULL;
	g_autoptr(GOutputStream) output_stream = NULL;

	rom_fn = g_build_filename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self)), "rom", NULL);
	if (!g_str_has_prefix(rom_fn, "/sys"))
		return TRUE;

	output_stream = fu_output_stream_from_path(rom_fn, error);
	if (output_stream == NULL)
		return FALSE;
	if (!g_output_stream_write_all(output_stream, value ? "1" : "0", 1, NULL, NULL, error)) {
		fwupd_error_convert(error);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_oprom_device_dump_enable_cb(FuDevice *device, GError **error)
{
	FuOpromDevice *self = FU_OPROM_DEVICE(device);
	return fu_oprom_device_set_enabled(self, TRUE, error);
}

static gboolean
fu_oprom_device_dump_disable_cb(FuDevice *device, GError **error)
{
	FuOpromDevice *self = FU_OPROM_DEVICE(device);
	return fu_oprom_device_set_enabled(self, FALSE, error);
}

static GBytes *
fu_oprom_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuOpromDevice *self = FU_OPROM_DEVICE(device);
	guint number_reads = 0;
	g_autofree gchar *rom_fn = NULL;
	g_autoptr(FuDeviceLocker) locker_enable = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unable to read firmware from device, 'rom' does not exist");
		return NULL;
	}

	/* open file */
	rom_fn = g_build_filename(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self)), "rom", NULL);
	file = g_file_new_for_path(rom_fn);
	stream = G_INPUT_STREAM(g_file_read(file, NULL, &error_local));
	if (stream == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AUTH_FAILED,
				    error_local->message);
		return NULL;
	}

	/* we have to enable the read for devices */
	locker_enable = fu_device_locker_new_full(device,
						  fu_oprom_device_dump_enable_cb,
						  fu_oprom_device_dump_disable_cb,
						  error);
	if (locker_enable == NULL)
		return NULL;

	/* ensure we got enough data to fill the buffer */
	while (TRUE) {
		gssize sz;
		guint8 tmp[32 * 1024] = {0x0};
		sz = g_input_stream_read(stream, tmp, sizeof(tmp), NULL, error);
		if (sz == 0)
			break;
		g_debug("ROM returned 0x%04x bytes", (guint)sz);
		if (sz < 0)
			return NULL;
		g_byte_array_append(buf, tmp, sz);

		/* check the firmware isn't serving us small chunks */
		if (number_reads++ > 1024) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "firmware not fulfilling requests");
			return NULL;
		}
	}
	if (buf->len < 512) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware too small: 0x%x bytes",
			    (guint)buf->len);
		return NULL;
	}
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf)); /* nocheck:blocked */
}

static void
fu_oprom_device_init(FuOpromDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
}

static void
fu_oprom_device_class_init(FuOpromDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->dump_firmware = fu_oprom_device_dump_firmware;
	device_class->probe = fu_oprom_device_probe;
}
