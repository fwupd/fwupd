/*
 * Copyright 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-hidpp-device.h"
#include "fu-logitech-hidpp-radio.h"

struct _FuLogitechHidppRadio {
	FuDevice parent_instance;
	guint8 entity;
};

G_DEFINE_TYPE(FuLogitechHidppRadio, fu_logitech_hidpp_radio, FU_TYPE_DEVICE)

static void
fu_logitech_hidpp_radio_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLogitechHidppRadio *self = FU_HIDPP_RADIO(device);
	fwupd_codec_string_append_hex(str, idt, "Entity", self->entity);
}

static gboolean
fu_logitech_hidpp_radio_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLogitechHidppRadio *self = FU_HIDPP_RADIO(device);
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return fu_logitech_hidpp_device_attach(FU_HIDPP_DEVICE(parent),
					       self->entity,
					       progress,
					       error);
}

static gboolean
fu_logitech_hidpp_radio_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;

	if (!fu_device_has_flag(parent, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return fu_device_detach_full(parent, progress, error);
}

static gboolean
fu_logitech_hidpp_radio_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GInputStream) stream = NULL;

	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* open */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_write_firmware(parent, stream, progress, flags, error);
}

static void
fu_logitech_hidpp_radio_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 96, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 3, "reload");
}

static void
fu_logitech_hidpp_radio_init(FuLogitechHidppRadio *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_vendor(FU_DEVICE(self), "Logitech");
	fu_device_set_name(FU_DEVICE(self), "Radio");
	fu_device_set_install_duration(FU_DEVICE(self), 270);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_BATTERY);
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.unifyingsigned");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
}

static void
fu_logitech_hidpp_radio_class_init(FuLogitechHidppRadioClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->detach = fu_logitech_hidpp_radio_detach;
	device_class->attach = fu_logitech_hidpp_radio_attach;
	device_class->write_firmware = fu_logitech_hidpp_radio_write_firmware;
	device_class->to_string = fu_logitech_hidpp_radio_to_string;
	device_class->set_progress = fu_logitech_hidpp_radio_set_progress;
}

FuLogitechHidppRadio *
fu_logitech_hidpp_radio_new(FuContext *ctx, guint8 entity)
{
	FuLogitechHidppRadio *self = NULL;

	self = g_object_new(FU_TYPE_LOGITECH_HIDPP_RADIO, "context", ctx, NULL);
	self->entity = entity;
	return self;
}
