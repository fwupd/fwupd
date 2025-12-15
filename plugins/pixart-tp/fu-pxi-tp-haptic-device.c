/* Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-pxi-tp-device.h"
#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-haptic-device.h"
#include "fu-pxi-tp-section.h"
#include "fu-pxi-tp-tf-communication.h"

struct _FuPxiTpHapticDevice {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuPxiTpHapticDevice, fu_pxi_tp_haptic_device, FU_TYPE_DEVICE)

static FuPxiTpDevice *
fu_pxi_tp_haptic_device_get_parent_tp(FuPxiTpHapticDevice *self, GError **error)
{
	FuDevice *parent = NULL;

	g_return_val_if_fail(FU_IS_PXI_TP_HAPTIC_DEVICE(self), NULL);

	parent = fu_device_get_parent(FU_DEVICE(self), error);
	if (parent == NULL)
		return NULL;

	if (!FU_IS_PXI_TP_DEVICE(parent)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "parent device is not FuPxiTpDevice");
		return NULL;
	}

	return FU_PXI_TP_DEVICE(parent);
}

static gboolean
fu_pxi_tp_haptic_device_setup(FuDevice *device, GError **error)
{
	FuPxiTpHapticDevice *self = FU_PXI_TP_HAPTIC_DEVICE(device);
	FuPxiTpDevice *parent_tp = NULL;
	guint8 ver[3] = {0};
	g_autofree gchar *ver_str = NULL;
	g_autoptr(GError) error_local = NULL;

	parent_tp = fu_pxi_tp_haptic_device_get_parent_tp(self, error);
	if (parent_tp == NULL)
		return FALSE;

	if (!fu_device_build_instance_id(device,
					 error,
					 "HIDRAW",
					 "VEN",
					 "DEV",
					 "COMPONENT",
					 NULL)) {
		return FALSE;
	}

	/* best-effort: if TF is not present or not responding, keep device online */
	if (!fu_pxi_tp_tf_communication_read_firmware_version(parent_tp,
							      FU_PXI_TF_FW_MODE_APP,
							      ver,
							      &error_local)) {
		if (error_local != NULL) {
			g_debug("haptic: failed to read TF firmware version: %s",
				error_local->message);
		}
		return TRUE;
	}

	ver_str = g_strdup_printf("%u.%u.%u", ver[0], ver[1], ver[2]);
	fu_device_set_version(device, ver_str);
	return TRUE;
}

static gboolean
fu_pxi_tp_haptic_device_reload(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* best-effort: do not fail the whole update just because reload failed */
	if (!fu_pxi_tp_haptic_device_setup(device, &error_local)) {
		if (error_local != NULL) {
			g_debug("haptic: failed to refresh TF firmware version: %s",
				error_local->message);
			g_clear_error(&error_local);
		}
	}

	(void)error;
	return TRUE;
}

static FuFirmware *
fu_pxi_tp_haptic_device_prepare_firmware(FuDevice *device,
					 GInputStream *stream,
					 FuProgress *progress,
					 FuFirmwareParseFlags flags,
					 GError **error)
{
	g_autoptr(FuFirmware) container = NULL;
	FuFirmware *img = NULL;
	g_autoptr(GError) error_local = NULL;

	(void)device;
	(void)progress;

	/* parse the TP FWHD container */
	container = fu_pxi_tp_firmware_new();
	if (!fu_firmware_parse_stream(container, stream, 0x0, flags, &error_local)) {
		if (error_local != NULL) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return NULL;
		}

		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "failed to parse TP FWHD container");
		return NULL;
	}

	/* find the TF_FORCE section image by ID */
	img = fu_firmware_get_image_by_id(container, "com.pixart.tf-force", error);
	if (img == NULL)
		return NULL;

	if (!FU_IS_PXI_TP_SECTION(img)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "TF_FORCE image is not FuPxiTpSection");
		return NULL;
	}

	return g_object_ref(img);
}

static gboolean
fu_pxi_tp_haptic_device_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuPxiTpHapticDevice *self = FU_PXI_TP_HAPTIC_DEVICE(device);
	FuPxiTpDevice *parent_tp = NULL;
	FuPxiTpSection *section = NULL;
	g_autoptr(GByteArray) reserved = NULL;
	guint8 target_ver[3] = {0};
	guint32 send_interval = 0;
	g_autoptr(GByteArray) payload = NULL;
	gsize len = 0;

	(void)flags;

	parent_tp = fu_pxi_tp_haptic_device_get_parent_tp(self, error);
	if (parent_tp == NULL)
		return FALSE;

	if (!FU_IS_PXI_TP_SECTION(firmware)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "TF firmware object is not FuPxiTpSection");
		return FALSE;
	}

	section = FU_PXI_TP_SECTION(firmware);

	/* ---- read version + send interval from reserved bytes ---- */
	reserved = fu_pxi_tp_section_get_reserved(section);
	if (reserved == NULL || reserved->len < 4) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "reserved bytes too short for TF_FORCE section");
		return FALSE;
	}

	target_ver[0] = reserved->data[0];
	target_ver[1] = reserved->data[1];
	target_ver[2] = reserved->data[2];
	send_interval = (guint32)reserved->data[3]; /* ms */

	/* ---- read TF payload ---- */
	payload = fu_pxi_tp_section_get_payload(section, error);
	if (payload == NULL)
		return FALSE;

	len = payload->len;
	if (len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "empty TF_FORCE payload");
		return FALSE;
	}

	/* call TF updater */
	if (!fu_pxi_tp_tf_communication_write_firmware_process(parent_tp,
							       progress,
							       send_interval,
							       (guint32)len,
							       payload,
							       target_ver,
							       error)) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_haptic_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device, NULL);

	(void)progress;

	if (parent == NULL)
		return TRUE;

	if (fu_device_has_flag(parent, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "cannot update TF while TP parent is in bootloader mode; "
				    "please replug the device or update the TP firmware first");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_haptic_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	/* TF upgrade/bootloader transitions are handled in
	 * fu_pxi_tp_tf_communication_write_firmware_process() and cleanup().
	 */
	return TRUE;
}

static void
fu_pxi_tp_haptic_device_set_progress(FuDevice *device, FuProgress *progress)
{
	(void)device;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_pxi_tp_haptic_device_cleanup(FuDevice *device,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuPxiTpHapticDevice *self = FU_PXI_TP_HAPTIC_DEVICE(device);
	FuPxiTpDevice *parent_tp = NULL;
	g_autoptr(GError) error_local = NULL;

	(void)progress;
	(void)flags;
	(void)error;

	parent_tp = fu_pxi_tp_haptic_device_get_parent_tp(self, &error_local);
	if (parent_tp == NULL) {
		if (error_local != NULL)
			g_clear_error(&error_local);
		return TRUE;
	}

	/* exit TF upgrade/engineer mode (best-effort) */
	if (!fu_pxi_tp_tf_communication_exit_upgrade_mode(parent_tp, &error_local)) {
		if (error_local != NULL) {
			g_debug("haptic: ignoring failure to exit TF upgrade mode in cleanup: %s",
				error_local->message);
			g_clear_error(&error_local);
		}
	}

	return TRUE;
}

static void
fu_pxi_tp_haptic_device_class_init(FuPxiTpHapticDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_pxi_tp_haptic_device_setup;
	device_class->reload = fu_pxi_tp_haptic_device_reload;
	device_class->prepare_firmware = fu_pxi_tp_haptic_device_prepare_firmware;
	device_class->write_firmware = fu_pxi_tp_haptic_device_write_firmware;
	device_class->attach = fu_pxi_tp_haptic_device_attach;
	device_class->detach = fu_pxi_tp_haptic_device_detach;
	device_class->set_progress = fu_pxi_tp_haptic_device_set_progress;
	device_class->cleanup = fu_pxi_tp_haptic_device_cleanup;
}

static void
fu_pxi_tp_haptic_device_init(FuPxiTpHapticDevice *self)
{
	FuDevice *device = FU_DEVICE(self);

	fu_device_add_protocol(device, "com.pixart.tp.haptic");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_add_instance_str(device, "COMPONENT", "tf");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_logical_id(device, "tf");
	fu_device_set_name(device, "Touchpad Haptic");
	fu_device_set_summary(device, "Force/haptic controller for touchpad");

	fu_device_add_icon(device, "input-touchpad");
}

FuPxiTpHapticDevice *
fu_pxi_tp_haptic_device_new(FuDevice *parent)
{
	if (parent == NULL)
		return NULL;

	return g_object_new(FU_TYPE_PXI_TP_HAPTIC_DEVICE, "parent", parent, NULL);
}
