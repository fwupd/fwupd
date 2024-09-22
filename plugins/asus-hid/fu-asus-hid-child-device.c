/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-asus-hid-child-device.h"
#include "fu-asus-hid-device.h"
#include "fu-asus-hid-firmware.h"

struct _FuAsusHidChildDevice {
	FuDevice parent_instance;
	guint8 idx;
};

G_DEFINE_TYPE(FuAsusHidChildDevice, fu_asus_hid_child_device, FU_TYPE_DEVICE)

static void
fu_asus_hid_child_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuAsusHidChildDevice *self = FU_ASUS_HID_CHILD_DEVICE(device);
	fwupd_codec_string_append_int(str, idt, "ChipIdx", self->idx);
}

static FuFirmware *
fu_asus_hid_child_device_prepare_firmware(FuDevice *device,
					  GInputStream *stream,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_asus_hid_firmware_new();
	const gchar *fw_pn;
	const gchar *logical;

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	fw_pn = fu_asus_hid_firmware_get_product(firmware);
	logical = fu_device_get_logical_id(device);
	if (g_strcmp0(fw_pn, logical) != 0) {
		if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "firmware for %s does not match %s",
				    fw_pn,
				    logical);
			return NULL;
		}
		g_warning("firmware for %s does not match %s but is being force installed anyway",
			  fw_pn,
			  logical);
	}

	return g_steal_pointer(&firmware);
}

static gboolean
fu_asus_hid_child_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);

	if (parent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no parent");
		return FALSE;
	}

	return fu_asus_hid_device_write_firmware(parent, firmware, progress, flags, error);
}

static gboolean
fu_asus_hid_child_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);

	if (parent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no parent");
		return FALSE;
	}

	return fu_device_attach(parent, error);
}

static gboolean
fu_asus_hid_child_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);

	if (parent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no parent");
		return FALSE;
	}

	return fu_device_detach(parent, error);
}

static void
fu_asus_hid_child_device_init(FuAsusHidChildDevice *self)
{
	// TODO: is it?
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
}

static void
fu_asus_hid_child_device_class_init(FuAsusHidChildDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_asus_hid_child_device_to_string;
	device_class->prepare_firmware = fu_asus_hid_child_device_prepare_firmware;
	device_class->write_firmware = fu_asus_hid_child_device_write_firmware;
	device_class->detach = fu_asus_hid_child_device_detach;
	device_class->attach = fu_asus_hid_child_device_attach;
}

FuDevice *
fu_asus_hid_child_device_new(FuContext *ctx, guint8 idx)
{
	FuDevice *dev = g_object_new(FU_TYPE_ASUS_HID_CHILD_DEVICE, "context", ctx, NULL);
	FuAsusHidChildDevice *self = FU_ASUS_HID_CHILD_DEVICE(dev);

	self->idx = idx;

	return dev;
}
