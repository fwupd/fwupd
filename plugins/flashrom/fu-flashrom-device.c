/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Daniel Campello <campello@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <libflashrom.h>

#include "fu-flashrom-cmos.h"
#include "fu-flashrom-device.h"

/*
 * Flag to determine if the CMOS checksum should be reset after the flash
 * is reprogrammed.  This will force the CMOS defaults to be reloaded on
 * the next boot.
 */
#define FU_FLASHROM_DEVICE_FLAG_RESET_CMOS (1 << 0)

struct _FuFlashromDevice {
	FuUdevDevice parent_instance;
	struct flashrom_flashctx *flashctx;
	struct flashrom_programmer *flashprog;
};

G_DEFINE_TYPE(FuFlashromDevice, fu_flashrom_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_flashrom_device_set_quirk_kv(FuDevice *device,
				const gchar *key,
				const gchar *value,
				GError **error)
{
	if (g_strcmp0(key, "PciBcrAddr") == 0) {
		guint64 tmp = 0;
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		fu_device_set_metadata_integer(device, "PciBcrAddr", tmp);
		return TRUE;
	}
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no supported");
	return FALSE;
}

static gboolean
fu_flashrom_device_probe(FuDevice *device, GError **error)
{
	const gchar *dev_name = NULL;
	const gchar *sysfs_path = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_flashrom_device_parent_class)->probe(device, error))
		return FALSE;

	sysfs_path = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	if (sysfs_path != NULL) {
		g_autofree gchar *physical_id = NULL;
		physical_id = g_strdup_printf("DEVNAME=%s", sysfs_path);
		fu_device_set_physical_id(device, physical_id);
	}
	dev_name = fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(device), "name", NULL);
	if (dev_name != NULL) {
		fu_device_add_instance_id_full(device,
					       dev_name,
					       FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}
	return TRUE;
}

static gboolean
fu_flashrom_device_open(FuDevice *device, GError **error)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(device);
	gint rc;

	if (flashrom_programmer_init(&self->flashprog, "internal", NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "programmer initialization failed");
		return FALSE;
	}
	rc = flashrom_flash_probe(&self->flashctx, self->flashprog, NULL);
	if (rc == 3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flash probe failed: multiple chips were found");
		return FALSE;
	}
	if (rc == 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flash probe failed: no chip was found");
		return FALSE;
	}
	if (rc != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flash probe failed: unknown error");
		return FALSE;
	}

	/* get the flash size from the device if not already been quirked */
	if (fu_device_get_firmware_size_max(device) == 0) {
		gsize flash_size = flashrom_flash_getsize(self->flashctx);
		if (flash_size == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "flash size zero");
			return FALSE;
		}
		fu_device_set_firmware_size_max(device, flash_size);
	}

	return TRUE;
}

static gboolean
fu_flashrom_device_close(FuDevice *device, GError **error)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(device);
	flashrom_flash_release(self->flashctx);
	flashrom_programmer_shutdown(self->flashprog);
	return TRUE;
}

static gboolean
fu_flashrom_device_prepare(FuDevice *device, FwupdInstallFlags flags, GError **error)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(device);
	g_autofree gchar *firmware_orig = NULL;
	g_autofree gchar *localstatedir = NULL;
	g_autofree gchar *basename = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* if the original firmware doesn't exist, grab it now */
	basename = g_strdup_printf("flashrom-%s.bin", fu_device_get_id(device));
	localstatedir = fu_common_get_path(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	firmware_orig = g_build_filename(localstatedir, "builder", basename, NULL);
	if (!fu_common_mkdir_parent(firmware_orig, error))
		return FALSE;
	if (!g_file_test(firmware_orig, G_FILE_TEST_EXISTS)) {
		struct flashrom_layout *layout;
		gsize flash_size = fu_device_get_firmware_size_max(device);
		g_autofree guint8 *newcontents = g_malloc0(flash_size);
		g_autoptr(GBytes) buf = NULL;

		if (flashrom_layout_read_from_ifd(&layout, self->flashctx, NULL, 0)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "failed to read layout from Intel ICH descriptor");
			return FALSE;
		}

		/* include bios region for safety reasons */
		if (flashrom_layout_include_region(layout, "bios")) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "invalid region name");
			return FALSE;
		}

		/* read region */
		flashrom_layout_set(self->flashctx, layout);

		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
		if (flashrom_image_read(self->flashctx, newcontents, flash_size)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "failed to back up original firmware");
			return FALSE;
		}
		buf = g_bytes_new_static(newcontents, flash_size);
		if (!fu_common_set_contents_bytes(firmware_orig, buf, error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_flashrom_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(device);
	struct flashrom_layout *layout;
	gsize sz = 0;
	gint rc;
	const guint8 *buf;
	g_autoptr(GBytes) blob_fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 10);

	/* read early */
	blob_fw = fu_firmware_get_bytes(firmware, error);
	if (blob_fw == NULL)
		return FALSE;
	buf = g_bytes_get_data(blob_fw, &sz);

	if (flashrom_layout_read_from_ifd(&layout, self->flashctx, NULL, 0)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "failed to read layout from Intel ICH descriptor");
		return FALSE;
	}

	/* include bios region for safety reasons */
	if (flashrom_layout_include_region(layout, "bios")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "invalid region name");
		return FALSE;
	}

	/* write region */
	flashrom_layout_set(self->flashctx, layout);
	if (sz != fu_device_get_firmware_size_max(device)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid image size 0x%x, expected 0x%x",
			    (guint)sz,
			    (guint)fu_device_get_firmware_size_max(device));
		return FALSE;
	}
	rc = flashrom_image_write(self->flashctx, (void *)buf, sz, NULL /* refbuffer */);
	if (rc != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "image write failed, err=%i",
			    rc);
		return FALSE;
	}
	fu_progress_step_done(progress);

	if (flashrom_image_verify(self->flashctx, (void *)buf, sz)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "image verify failed");
		return FALSE;
	}
	fu_progress_step_done(progress);
	flashrom_layout_release(layout);

	/* Check if CMOS needs a reset */
	if (fu_device_has_private_flag(device, FU_FLASHROM_DEVICE_FLAG_RESET_CMOS)) {
		g_debug("Attempting CMOS Reset");
		if (!fu_flashrom_cmos_reset(error)) {
			g_prefix_error(error, "failed CMOS reset: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_flashrom_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100); /* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0);	/* reload */
}

static void
fu_flashrom_device_init(FuFlashromDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_protocol(FU_DEVICE(self), "org.flashrom");
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ENSURE_SEMVER);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_SIGNED);
	fu_device_set_physical_id(FU_DEVICE(self), "flashrom");
	fu_device_set_logical_id(FU_DEVICE(self), "bios");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_icon(FU_DEVICE(self), "computer");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_FLASHROM_DEVICE_FLAG_RESET_CMOS,
					"reset-cmos");
}

static void
fu_flashrom_device_constructed(GObject *obj)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(obj);
	fu_device_add_instance_id(FU_DEVICE(self), "main-system-firmware");
}

static void
fu_flashrom_device_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_flashrom_device_parent_class)->finalize(object);
}

static void
fu_flashrom_device_class_init(FuFlashromDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->constructed = fu_flashrom_device_constructed;
	object_class->finalize = fu_flashrom_device_finalize;
	klass_device->set_quirk_kv = fu_flashrom_device_set_quirk_kv;
	klass_device->probe = fu_flashrom_device_probe;
	klass_device->open = fu_flashrom_device_open;
	klass_device->close = fu_flashrom_device_close;
	klass_device->set_progress = fu_flashrom_device_set_progress;
	klass_device->prepare = fu_flashrom_device_prepare;
	klass_device->write_firmware = fu_flashrom_device_write_firmware;
}

FuDevice *
fu_flashrom_device_new(FuContext *ctx)
{
	return FU_DEVICE(g_object_new(FU_TYPE_FLASHROM_DEVICE, "context", ctx, NULL));
}
