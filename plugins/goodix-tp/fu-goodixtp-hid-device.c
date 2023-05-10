/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-goodixtp-common.h"
#include "fu-goodixtp-firmware.h"
#include "fu-goodixtp-hid-device.h"

struct _FuGoodixtpHidDevice {
	FuUdevDevice parent_instance;
	struct goodix_hw_ops_t *hw_ops;
	gint ic_type;
	guint8 firmware_flag;
	guint8 patch_pid[9];
	guint8 patch_vid[4];
	guint32 sensor_id;
	guint8 cfg_ver;
	guint32 cfg_id;
	gint version;
};

G_DEFINE_TYPE(FuGoodixtpHidDevice, fu_goodixtp_hid_device, FU_TYPE_UDEV_DEVICE)

static gint
fu_goodixtp_judge_ic_type_from_pid(guint16 pid)
{
	if ((pid >= 0x01E0 && pid <= 0x01E7) || (pid >= 0x0D00 && pid <= 0x0D7F))
		return IC_TYPE_NORMANDYL;
	if ((pid >= 0x0EB0 && pid <= 0x0EBF) || (pid >= 0x0EC0 && pid <= 0x0ECF) ||
	    (pid >= 0x0EA5 && pid <= 0x0EAA) || (pid >= 0x0C00 && pid <= 0x0CFF))
		return IC_TYPE_BERLINB;

	return IC_TYPE_NONE;
}

static void
fu_goodixtp_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(device);
	gchar tmp_str[10] = {'G', 'T'};

	memcpy(tmp_str + 2, self->patch_pid, 8);
	fu_string_append(str, idt, "patch_pid", tmp_str);
	fu_string_append_kx(str,
			    idt,
			    "patch_vid",
			    fu_memread_uint32(self->patch_vid, G_BIG_ENDIAN));
	fu_string_append_kx(str, idt, "sensor_id", self->sensor_id);
	fu_string_append_kx(str, idt, "cfg_ver", self->cfg_ver);
	fu_string_append_kx(str, idt, "cfg_id", self->cfg_id);
	fu_string_append_kx(str, idt, "version", self->version);
}

static gboolean
fu_goodixtp_hid_device_probe(FuDevice *device, GError **error)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(device);
	gint ic_type;
	guint16 hid_pid;

	hid_pid = fu_udev_device_get_model(FU_UDEV_DEVICE(device));
	/* check is valid */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem=%s, expected hidraw",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	ic_type = fu_goodixtp_judge_ic_type_from_pid(hid_pid);
	if (ic_type == IC_TYPE_NONE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "can't find valid ic_type, pid is %x",
			    hid_pid);
		return FALSE;
	}

	if (ic_type == IC_TYPE_NORMANDYL) {
		self->firmware_flag = 0x0C;
		self->hw_ops = &gtx8_hw_ops;
	} else if (ic_type == IC_TYPE_BERLINB) {
		self->firmware_flag = 0x0B;
		self->hw_ops = &brlb_hw_ops;
	}

	self->ic_type = ic_type;

	/* set the physical ID */
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "hid", error);
}

static gboolean
fu_goodixtp_hid_device_setup(FuDevice *device, GError **error)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(device);
	struct goodix_version_t version;

	memset(&version, 0, sizeof(version));
	if (!self->hw_ops->get_version(device, &version, error)) {
		return FALSE;
	}
	memcpy(self->patch_pid, version.patch_pid, sizeof(version.patch_pid));
	memcpy(self->patch_vid, version.patch_vid, sizeof(version.patch_vid));
	self->sensor_id = version.sensor_id;
	self->cfg_ver = version.cfg_ver;
	self->cfg_id = version.cfg_id;
	self->version = version.version;

	fu_device_set_version_from_uint32(device, (guint32)self->version);
	return TRUE;
}

static FuFirmware *
fu_goodixtp_hid_device_prepare_firmware(FuDevice *device,
					GBytes *fw,
					FwupdInstallFlags flags,
					GError **error)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_goodixtp_firmware_new();

	if (!fu_goodixtp_frmware_parse(firmware, fw, self->ic_type, self->sensor_id, error)) {
		g_debug("parse firmware failed");
		return NULL;
	}

	return g_steal_pointer(&firmware);
}

static gboolean
fu_goodixtp_hid_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(device);
	FuGoodixtpFirmware *firmware_goodixtp = FU_GOODIXTP_FIRMWARE(firmware);
	guint8 *buf = fu_goodixtp_firmware_get_data(firmware_goodixtp);
	gint bufsz = fu_goodixtp_firmware_get_len(firmware_goodixtp);
	gint fw_ver = fu_goodixtp_firmware_get_version(firmware_goodixtp);
	guint8 has_config = fu_goodixtp_firmware_has_config(firmware_goodixtp);
	struct goodix_version_t ic_ver;
	g_autoptr(GPtrArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DOWNLOADING, 85, "download");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "reload");

	if (!self->hw_ops->update_prepare(device, error))
		return FALSE;
	fu_progress_step_done(progress);

	chunks = fu_chunk_array_new(buf, bufsz, 0x0, 0x0, RAM_BUFFER_SIZE);
	for (gint i = 0; i < (gint)chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint32 addr = fu_goodixtp_firmware_get_addr(firmware_goodixtp, i);
		if (!self->hw_ops->update_process(device,
						  addr,
						  (guint8 *)fu_chunk_get_data(chk),
						  fu_chunk_get_data_sz(chk),
						  error)) {
			return FALSE;
		}
		fu_device_sleep(device, 20);
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)chunks->len);
	}
	fu_progress_step_done(progress);

	if (!self->hw_ops->update_finish(device, error))
		return FALSE;
	if (!self->hw_ops->get_version(device, &ic_ver, error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!has_config)
		ic_ver.version &= ~0x000000FF;
	if (ic_ver.version != fw_ver) {
		g_debug("update failed chip_ver:%x != bin_ver:%x",
			(guint)ic_ver.version,
			(guint)fw_ver);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "update failed chip_ver:%x != bin_ver:%x",
			    (guint)ic_ver.version,
			    (guint)fw_ver);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_goodixtp_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_goodixtp_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static void
fu_goodixtp_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_goodixtp_hid_device_init(FuGoodixtpHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_summary(FU_DEVICE(self), "Touchpad");
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
	fu_device_add_protocol(FU_DEVICE(self), "com.goodix.goodixtp");
	fu_device_set_name(FU_DEVICE(self), "Touch Controller Sensor");
	fu_device_set_vendor(FU_DEVICE(self), "Goodix inc.");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_priority(FU_DEVICE(self), 1); /* better than i2c */
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
				 FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
				     FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK);
}

static void
fu_goodixtp_hid_device_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_goodixtp_hid_device_parent_class)->finalize(object);
}

static void
fu_goodixtp_hid_device_class_init(FuGoodixtpHidDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	object_class->finalize = fu_goodixtp_hid_device_finalize;
	klass_device->to_string = fu_goodixtp_hid_device_to_string;
	klass_device->attach = fu_goodixtp_hid_device_attach;
	klass_device->detach = fu_goodixtp_hid_device_detach;
	klass_device->setup = fu_goodixtp_hid_device_setup;
	klass_device->reload = fu_goodixtp_hid_device_setup;
	klass_device->prepare_firmware = fu_goodixtp_hid_device_prepare_firmware;
	klass_device->write_firmware = fu_goodixtp_hid_device_write_firmware;
	klass_device->probe = fu_goodixtp_hid_device_probe;
	klass_device->set_progress = fu_goodixtp_hid_device_set_progress;
}
