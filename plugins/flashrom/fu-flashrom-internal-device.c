/*
 * Copyright (C) 2021 Daniel Campello <campello@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-flashrom-device.h"
#include "fu-flashrom-internal-device.h"

struct _FuFlashromInternalDevice {
	FuFlashromDevice		 parent_instance;
};

G_DEFINE_TYPE (FuFlashromInternalDevice, fu_flashrom_internal_device,
	       FU_TYPE_FLASHROM_DEVICE)

static void
fu_flashrom_internal_device_init (FuFlashromInternalDevice *self)
{
	FuFlashromOpener *opener = fu_flashrom_device_get_opener (FU_FLASHROM_DEVICE (self));

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

	fu_flashrom_opener_set_programmer (opener, "internal");
	fu_flashrom_opener_set_layout_from_ifd (opener);
}

static gboolean
fu_flashrom_internal_device_prepare (FuDevice *device,
				     FwupdInstallFlags flags,
				     GError **error)
{
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
		g_autoptr(FuFlashromContext) ctx =
			fu_flashrom_device_get_context (FU_FLASHROM_DEVICE (device));
		g_autoptr(GBytes) buf = NULL;

		if (!fu_flashrom_context_read_image (ctx, &buf, error)) {
			g_prefix_error(error, "failed to back up original firmware: ");
			return FALSE;
		}

		if (!fu_common_set_contents_bytes (firmware_orig, buf, error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_flashrom_internal_device_write_firmware (FuDevice *device,
					    FuFirmware *firmware,
					    FwupdInstallFlags flags,
					    GError **error)
{
	g_autoptr(GBytes) blob_fw = fu_firmware_get_bytes (firmware, error);
	FuFlashromContext *flashctx = fu_flashrom_device_get_context(
		FU_FLASHROM_DEVICE (device));

	if (blob_fw == NULL)
		return FALSE;

	if (!fu_flashrom_context_set_included_regions (flashctx, error,
						       "bios", NULL))
		return FALSE;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	fu_device_set_progress (device, 0); /* urgh */
	if (!fu_flashrom_context_write_image (flashctx, blob_fw, FALSE, error))
		return FALSE;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);
	if (!fu_flashrom_context_verify_image (flashctx, blob_fw, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_flashrom_internal_device_class_init (FuFlashromInternalDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->prepare = fu_flashrom_internal_device_prepare;
	klass_device->write_firmware = fu_flashrom_internal_device_write_firmware;
}

FuDevice *
fu_flashrom_internal_device_new (void)
{
	return FU_DEVICE (g_object_new (FU_TYPE_FLASHROM_INTERNAL_DEVICE, NULL));
}
