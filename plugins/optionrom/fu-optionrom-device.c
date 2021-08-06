/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-optionrom-device.h"

struct _FuOptionromDevice {
	FuUdevDevice		 parent_instance;
};

G_DEFINE_TYPE (FuOptionromDevice, fu_optionrom_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_optionrom_device_probe (FuDevice *device, GError **error)
{
	g_autofree gchar *fn = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS (fu_optionrom_device_parent_class)->probe (device, error))
		return FALSE;

	/* does the device even have ROM? */
	fn = g_build_filename (fu_udev_device_get_sysfs_path (FU_UDEV_DEVICE (device)), "rom", NULL);
	if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Unable to read firmware from device");
		return FALSE;
	}

	/* set the physical ID */
	return fu_udev_device_set_physical_id (FU_UDEV_DEVICE (device), "pci", error);
}

static GBytes *
fu_optionrom_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuUdevDevice *udev_device = FU_UDEV_DEVICE (device);
	guint number_reads = 0;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *rom_fn = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new ();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* open the file */
	rom_fn = g_build_filename (fu_udev_device_get_sysfs_path (udev_device), "rom", NULL);
	if (rom_fn == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Unable to read firmware from device");
		return NULL;
	}

	/* open file */
	file = g_file_new_for_path (rom_fn);
	stream = G_INPUT_STREAM (g_file_read (file, NULL, &error_local));
	if (stream == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_AUTH_FAILED,
				     error_local->message);
		return NULL;
	}

	/* we have to enable the read for devices */
	fn = g_file_get_path (file);
	if (g_str_has_prefix (fn, "/sys")) {
		g_autoptr(GFileOutputStream) output_stream = NULL;
		output_stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE,
						NULL, error);
		if (output_stream == NULL)
			return NULL;
		if (g_output_stream_write (G_OUTPUT_STREAM (output_stream), "1", 1,
					   NULL, error) < 0)
			return NULL;
	}

	/* ensure we got enough data to fill the buffer */
	while (TRUE) {
		gssize sz;
		guint8 tmp[32 * 1024] = { 0x0 };
		sz = g_input_stream_read (stream, tmp, sizeof(tmp), NULL, error);
		if (sz == 0)
			break;
		g_debug ("ROM returned 0x%04x bytes", (guint) sz);
		if (sz < 0)
			return NULL;
		g_byte_array_append (buf, tmp, sz);

		/* check the firmware isn't serving us small chunks */
		if (number_reads++ > 1024) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "firmware not fulfilling requests");
			return NULL;
		}
	}
	if (buf->len < 512) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware too small: %u bytes", buf->len);
		return NULL;
	}
	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

static void
fu_optionrom_device_init (FuOptionromDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon (FU_DEVICE (self), "audio-card");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_set_logical_id (FU_DEVICE (self), "rom");
	fu_udev_device_set_flags (FU_UDEV_DEVICE (self),
				  FU_UDEV_DEVICE_FLAG_OPEN_READ |
				  FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT);
}

static void
fu_optionrom_device_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_optionrom_device_parent_class)->finalize (object);
}

static void
fu_optionrom_device_class_init (FuOptionromDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_optionrom_device_finalize;
	klass_device->dump_firmware = fu_optionrom_device_dump_firmware;
	klass_device->probe = fu_optionrom_device_probe;
}
