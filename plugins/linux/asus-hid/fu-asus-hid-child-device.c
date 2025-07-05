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

#define FU_ASUS_HID_CHILD_DEVICE_TIMEOUT 200 /* ms */

static void
fu_asus_hid_child_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuAsusHidChildDevice *self = FU_ASUS_HID_CHILD_DEVICE(device);
	fwupd_codec_string_append_int(str, idt, "ChipIdx", self->idx);
}

static gboolean
fu_asus_hid_child_device_transfer_feature(FuAsusHidChildDevice *self,
					  GByteArray *req,
					  GByteArray *res,
					  guint8 report,
					  GError **error)
{
	FuHidrawDevice *hid_dev = FU_HIDRAW_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));

	if (req != NULL) {
		if (!fu_hidraw_device_set_feature(hid_dev,
						  req->data,
						  req->len,
						  FU_IOCTL_FLAG_NONE,
						  error)) {
			g_prefix_error(error, "failed to send packet: ");
			return FALSE;
		}
	}
	if (res != NULL) {
		if (!fu_hidraw_device_get_feature(hid_dev,
						  res->data,
						  res->len,
						  FU_IOCTL_FLAG_NONE,
						  error)) {
			g_prefix_error(error, "failed to receive packet: ");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_asus_hid_child_device_ensure_manufacturer(FuAsusHidChildDevice *self, GError **error)
{
	g_autofree gchar *man = NULL;
	g_autoptr(FuStructAsusManCommand) cmd = fu_struct_asus_man_command_new();
	g_autoptr(FuStructAsusManResult) result = fu_struct_asus_man_result_new();

	if (!fu_asus_hid_child_device_transfer_feature(self,
						       cmd,
						       result,
						       FU_ASUS_HID_REPORT_ID_INFO,
						       error))
		return FALSE;

	man = fu_struct_asus_man_result_get_data(result);
	if (g_strcmp0(man, "ASUSTech.Inc.") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "manufacturer %s not supported",
			    man);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_asus_hid_child_device_ensure_version(FuAsusHidChildDevice *self, GError **error)
{
	g_autoptr(FuStructAsusHidCommand) cmd = fu_struct_asus_hid_command_new();
	g_autoptr(FuStructAsusHidFwInfo) result = fu_struct_asus_hid_fw_info_new();
	g_autoptr(FuStructAsusHidFwInfo) fw_info = NULL;
	g_autofree gchar *version = NULL;

	if (self->idx == FU_ASUS_HID_CONTROLLER_PRIMARY)
		fu_struct_asus_hid_command_set_cmd(cmd, FU_ASUS_HID_COMMAND_FW_VERSION);
	else if (self->idx == FU_ASUS_HID_CONTROLLER_MAIN)
		fu_struct_asus_hid_command_set_cmd(cmd, FU_ASUS_HID_COMMAND_MAIN_FW_VERSION);
	else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "mcu not supported");
		return FALSE;
	}

	fu_struct_asus_hid_command_set_length(cmd, FU_STRUCT_ASUS_HID_RESULT_SIZE);

	if (!fu_asus_hid_child_device_transfer_feature(self,
						       cmd,
						       result,
						       FU_ASUS_HID_REPORT_ID_INFO,
						       error))
		return FALSE;

	fw_info = fu_struct_asus_hid_fw_info_get_description(result);
	version = fu_struct_asus_hid_desc_get_version(fw_info);
	fu_device_set_version(FU_DEVICE(self), version);

	if (fu_device_get_logical_id(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *product = fu_struct_asus_hid_desc_get_product(fw_info);
		fu_device_add_instance_strsafe(FU_DEVICE(self), "PART", product);
		fu_device_build_instance_id(FU_DEVICE(self),
					    NULL,
					    "HIDRAW",
					    "VEN",
					    "DEV",
					    "PART",
					    NULL);

		fu_device_set_logical_id(FU_DEVICE(self), product);
	}

	return TRUE;
}

static gboolean
fu_asus_hid_child_device_setup(FuDevice *device, GError **error)
{
	FuAsusHidChildDevice *self = FU_ASUS_HID_CHILD_DEVICE(device);
	g_autofree gchar *name = NULL;

	if (fu_device_get_proxy(FU_DEVICE(self)) == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	name = g_strdup_printf("Microcontroller %u", self->idx);
	fu_device_set_name(FU_DEVICE(self), name);

	if (fu_device_has_flag(fu_device_get_proxy(FU_DEVICE(self)),
			       FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_autofree gchar *recovery_str = g_strdup_printf("%d", self->idx);
		// RC71LS = 0
		// RC71LM = 1
		fu_device_add_instance_strsafe(FU_DEVICE(self), "RECOVERY", recovery_str);
		fu_device_build_instance_id(FU_DEVICE(self),
					    NULL,
					    "USB",
					    "VID",
					    "PID",
					    "RECOVERY",
					    NULL);
		fu_device_set_logical_id(FU_DEVICE(self), recovery_str);
		fu_device_set_version(FU_DEVICE(self), "0");
		return TRUE;
	}

	if (!fu_asus_hid_child_device_ensure_manufacturer(self, error)) {
		g_prefix_error(error, "failed to ensure manufacturer: ");
		return FALSE;
	}
	if (!fu_asus_hid_child_device_ensure_version(self, error)) {
		g_prefix_error(error, "failed to ensure version: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_asus_hid_child_device_reload(FuDevice *device, GError **error)
{
	FuAsusHidChildDevice *self = FU_ASUS_HID_CHILD_DEVICE(device);

	return fu_asus_hid_child_device_ensure_version(self, error);
}

static gboolean
fu_asus_hid_child_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);

	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	return fu_device_attach(proxy, error);
}

static gboolean
fu_asus_hid_child_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);

	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	return fu_device_detach(proxy, error);
}

static void
fu_asus_hid_child_device_init(FuAsusHidChildDevice *self)
{
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FALLBACK);
	fu_device_add_protocol(FU_DEVICE(self), "com.asus.hid");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
}

static void
fu_asus_hid_child_device_class_init(FuAsusHidChildDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_asus_hid_child_device_to_string;
	device_class->detach = fu_asus_hid_child_device_detach;
	device_class->attach = fu_asus_hid_child_device_attach;
	device_class->setup = fu_asus_hid_child_device_setup;
	device_class->reload = fu_asus_hid_child_device_reload;
}

FuDevice *
fu_asus_hid_child_device_new(FuDevice *proxy, guint8 idx)
{
	FuDevice *dev = g_object_new(FU_TYPE_ASUS_HID_CHILD_DEVICE, "proxy", proxy, NULL);
	FuAsusHidChildDevice *self = FU_ASUS_HID_CHILD_DEVICE(dev);

	self->idx = idx;

	return dev;
}
