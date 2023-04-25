/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <linux/hidraw.h>
#include <linux/input.h>

#include "fu-goodixtp-common.h"
#include "fu-goodixtp-hid-device.h"

struct _FuGoodixtpHidDevice {
	FuUdevDevice parent_instance;
	struct goodix_hw_ops_t *hw_ops;
	guint32 firmware_flag;
	guint16 pid;
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

static gboolean
fu_goodixtp_hid_device_probe(FuDevice *device, GError **error)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(device);
	gint ic_type;

	self->pid = fu_udev_device_get_model(FU_UDEV_DEVICE(device));
	/* check is valid */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem=%s, expected hidraw",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	ic_type = fu_goodixtp_judge_ic_type_from_pid(self->pid);
	if (ic_type == IC_TYPE_NONE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "can't find valid ic_type, pid is %x",
			    self->pid);
		return FALSE;
	}

	if (ic_type == IC_TYPE_NORMANDYL) {
		self->firmware_flag = 0x0C;
		self->hw_ops = &gtx8_hw_ops;
	} else if (ic_type == IC_TYPE_BERLINB) {
		self->firmware_flag = 0x0B;
		self->hw_ops = &brlb_hw_ops;
	}

	if (self->hw_ops->get_version == NULL || self->hw_ops->parse_firmware == NULL ||
	    self->hw_ops->update_prepare == NULL || self->hw_ops->update == NULL ||
	    self->hw_ops->update_finish == NULL) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "hw_ops is NULL");
		return FALSE;
	}

	/* set the physical ID */
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "hid", error);
}

static gboolean
fu_goodixtp_hid_device_setup(FuDevice *device, GError **error)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(device);

	self->version = self->hw_ops->get_version(device, error);
	if (self->version < 0)
		return FALSE;
	fu_device_set_version_from_uint32(device, (guint32)self->version);
	return TRUE;
}

static gboolean
fu_goodixtp_hid_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuGoodixtpHidDevice *self = FU_GOODIXTP_HID_DEVICE(device);
	g_autoptr(GBytes) fw = NULL;
	const guint8 *buf;
	gsize bufsz = 0;
	gboolean ret;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 10, "parse_firmware");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DOWNLOADING, 70, "download");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "reload");

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	buf = g_bytes_get_data(fw, &bufsz);

	ret = self->hw_ops->parse_firmware(device, (guint8 *)buf, (guint32)bufsz, error);
	if (!ret) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "parse firmware failed");
		return FALSE;
	}
	fu_progress_step_done(progress);

	ret = self->hw_ops->update_prepare(device, error);
	if (!ret)
		return FALSE;
	fu_progress_step_done(progress);

	ret = self->hw_ops->update(device, self->firmware_flag, error);
	if (!ret)
		return FALSE;
	fu_progress_step_done(progress);

	ret = self->hw_ops->update_finish(device, error);
	if (!ret)
		return FALSE;
	fu_progress_step_done(progress);

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
	klass_device->attach = fu_goodixtp_hid_device_attach;
	klass_device->detach = fu_goodixtp_hid_device_detach;
	klass_device->setup = fu_goodixtp_hid_device_setup;
	klass_device->reload = fu_goodixtp_hid_device_setup;
	klass_device->write_firmware = fu_goodixtp_hid_device_write_firmware;
	klass_device->probe = fu_goodixtp_hid_device_probe;
	klass_device->set_progress = fu_goodixtp_hid_device_set_progress;
}
