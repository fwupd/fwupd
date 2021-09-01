/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-logitech-hidpp-device.h"
#include "fu-logitech-hidpp-radio.h"

struct _FuLogitechHidPpRadio {
	FuDevice parent_instance;
	guint8 entity;
};

G_DEFINE_TYPE(FuLogitechHidPpRadio, fu_logitech_hidpp_radio, FU_TYPE_DEVICE)

static void
fu_logitech_hidpp_radio_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLogitechHidPpRadio *self = FU_HIDPP_RADIO(device);
	fu_common_string_append_kx(str, idt, "Entity", self->entity);
}

static gboolean
fu_logitech_hidpp_radio_attach(FuDevice *device, GError **error)
{
	FuLogitechHidPpRadio *self = FU_HIDPP_RADIO(device);
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;

	fu_device_set_status(device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return fu_logitech_hidpp_device_attach(FU_HIDPP_DEVICE(parent), self->entity, error);
}

static gboolean
fu_logitech_hidpp_radio_detach(FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;

	if (!fu_device_has_flag(parent, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		fu_device_set_status(device, FWUPD_STATUS_DEVICE_RESTART);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	}
	return fu_device_detach(parent, error);
}

static gboolean
fu_logitech_hidpp_radio_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GBytes) fw = NULL;

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* open */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;

	fu_device_set_status(device, FWUPD_STATUS_DEVICE_WRITE);
	return fu_device_write_firmware(parent, fw, flags, error);
}

static void
fu_logitech_hidpp_radio_init(FuLogitechHidPpRadio *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_name(FU_DEVICE(self), "Radio");
	fu_device_set_install_duration(FU_DEVICE(self), 270);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_internal_flag(FU_DEVICE(self),
				    FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_BATTERY);
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.unifyingsigned");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
}

static void
fu_logitech_hidpp_radio_class_init(FuLogitechHidPpRadioClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	klass_device->detach = fu_logitech_hidpp_radio_detach;
	klass_device->attach = fu_logitech_hidpp_radio_attach;
	klass_device->write_firmware = fu_logitech_hidpp_radio_write_firmware;
	klass_device->to_string = fu_logitech_hidpp_radio_to_string;
}

FuLogitechHidPpRadio *
fu_logitech_hidpp_radio_new(FuContext *ctx, guint8 entity)
{
	FuLogitechHidPpRadio *self = NULL;

	self = g_object_new(FU_TYPE_LOGITECH_HIDPP_RADIO, "context", ctx, NULL);
	self->entity = entity;
	return self;
}
