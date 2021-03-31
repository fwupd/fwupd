/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-flashrom-device.h"

#include <libflashrom.h>

struct _FuFlashromDevice {
	FuDevice			 parent_instance;
	gsize				 flash_size;
	struct flashrom_flashctx	*flashctx;
	struct flashrom_layout		*layout;
	struct flashrom_programmer	*flashprog;
};

G_DEFINE_TYPE (FuFlashromDevice, fu_flashrom_device, FU_TYPE_DEVICE)

static void
fu_flashrom_device_init (FuFlashromDevice *self)
{
	fu_device_add_protocol (FU_DEVICE (self), "org.flashrom");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_instance_id (FU_DEVICE (self), "main-system-firmware");
	fu_device_add_internal_flag (FU_DEVICE (self), FU_DEVICE_INTERNAL_FLAG_ENSURE_SEMVER);
	fu_device_set_physical_id (FU_DEVICE (self), "flashrom");
	fu_device_set_logical_id (FU_DEVICE (self), "bios");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_icon (FU_DEVICE (self), "computer");
}

static void
fu_flashrom_device_finalize (GObject *object)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE (object);
	flashrom_layout_release (self->layout);
	flashrom_programmer_shutdown (self->flashprog);
	flashrom_flash_release (self->flashctx);
	G_OBJECT_CLASS (fu_flashrom_device_parent_class)->finalize (object);
}

static gboolean
fu_flashrom_device_set_quirk_kv (FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	if (g_strcmp0 (key, "PciBcrAddr") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		fu_device_set_metadata_integer (device, "PciBcrAddr", tmp);
		return TRUE;
	}
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "no supported");
	return FALSE;
}

static gboolean
fu_flashrom_device_setup (FuDevice *device, GError **error)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE (device);
	gint rc;

	if (flashrom_programmer_init (&self->flashprog, "internal", NULL)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "programmer initialization failed");
		return FALSE;
	}
	rc = flashrom_flash_probe (&self->flashctx, self->flashprog, NULL);
	if (rc == 3) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "flash probe failed: multiple chips were found");
		return FALSE;
	}
	if (rc == 2) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "flash probe failed: no chip was found");
		return FALSE;
	}
	if (rc != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "flash probe failed: unknown error");
		return FALSE;
	}
	self->flash_size = flashrom_flash_getsize (self->flashctx);
	if (self->flash_size == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "flash size zero");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_flashrom_device_prepare (FuDevice *device,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE (device);
	g_autofree gchar *firmware_orig = NULL;
	g_autofree gchar *localstatedir = NULL;
	g_autofree gchar *basename = NULL;

	/* if the original firmware doesn't exist, grab it now */
	basename = g_strdup_printf ("flashrom-%s.bin", fu_device_get_id (device));
	localstatedir = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
	firmware_orig = g_build_filename (localstatedir, "builder", basename, NULL);
	if (!fu_common_mkdir_parent (firmware_orig, error))
		return FALSE;
	if (!g_file_test (firmware_orig, G_FILE_TEST_EXISTS)) {
		g_autofree guint8 *newcontents = g_malloc0 (self->flash_size);
		g_autoptr(GBytes) buf = NULL;

		fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);
		if (flashrom_image_read (self->flashctx, newcontents, self->flash_size)) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     "failed to back up original firmware");
			return FALSE;
		}
		buf = g_bytes_new_static (newcontents, self->flash_size);
		if (!fu_common_set_contents_bytes (firmware_orig, buf, error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_flashrom_device_write_firmware (FuDevice *device,
				   FuFirmware *firmware,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE (device);
	gsize sz = 0;
	gint rc;
	const guint8 *buf;
	g_autoptr(GBytes) blob_fw = fu_firmware_get_bytes (firmware, error);
	if (blob_fw == NULL)
		return FALSE;

	buf = g_bytes_get_data (blob_fw, &sz);

	if (flashrom_layout_read_from_ifd (&self->layout, self->flashctx, NULL, 0)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "failed to read layout from Intel ICH descriptor");
		return FALSE;
	}

	/* include bios region for safety reasons */
	if (flashrom_layout_include_region (self->layout, "bios")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid region name");
		return FALSE;
	}

	/* write region */
	flashrom_layout_set (self->flashctx, self->layout);
	if (sz != self->flash_size) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid image size 0x%x, expected 0x%x",
			     (guint) sz, (guint) self->flash_size);
		return FALSE;
	}

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	fu_device_set_progress (device, 0); /* urgh */
	rc = flashrom_image_write (self->flashctx, (void *) buf, sz, NULL /* refbuffer */);
	if (rc != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "image write failed, err=%i", rc);
		return FALSE;
	}

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);
	if (flashrom_image_verify (self->flashctx, (void *) buf, sz)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "image verify failed");
		return FALSE;
	}

	/* success */
	return TRUE;
}
static void
fu_flashrom_device_class_init (FuFlashromDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_flashrom_device_finalize;
	klass_device->set_quirk_kv = fu_flashrom_device_set_quirk_kv;
	klass_device->setup = fu_flashrom_device_setup;
	klass_device->prepare = fu_flashrom_device_prepare;
	klass_device->write_firmware = fu_flashrom_device_write_firmware;
}

FuDevice *
fu_flashrom_device_new (void)
{
	return FU_DEVICE (g_object_new (FU_TYPE_FLASHROM_DEVICE, NULL));
}
