/*
 * Copyright 2025 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-legion-hid2-child-device.h"
#include "fu-legion-hid2-device.h"
#include "fu-legion-hid2-struct.h"

struct _FuLegionHid2ChildDevice {
	FuDevice parent_instance;
	guint8 manufacturer;
};

G_DEFINE_TYPE(FuLegionHid2ChildDevice, fu_legion_hid2_child_device, FU_TYPE_DEVICE)

#define FU_LEGION_HID2_CHILD_DEVICE_TIMEOUT 200 /* ms */

static void
fu_legion_hid2_child_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLegionHid2ChildDevice *self = FU_LEGION_HID2_CHILD_DEVICE(device);
	fwupd_codec_string_append_int(str, idt, "ChipManufacturer", self->manufacturer);
}

static gboolean
fu_legion_hid2_child_device_transfer(FuLegionHid2ChildDevice *self,
				     GByteArray *req,
				     GByteArray *res,
				     GError **error)
{
	FuHidDevice *hid_dev = FU_HID_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));

	if (req != NULL) {
		if (!fu_hid_device_set_report(hid_dev,
					      req->data[0],
					      req->data,
					      req->len,
					      FU_LEGION_HID2_CHILD_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					      error)) {
			g_prefix_error(error, "failed to send packet: ");
			return FALSE;
		}
	}
	if (res != NULL) {
		if (!fu_hid_device_get_report(hid_dev,
					      res->data[0],
					      res->data,
					      res->len,
					      FU_LEGION_HID2_CHILD_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					      error)) {
			g_prefix_error(error, "failed to receive packet: ");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_legion_hid2_child_device_probe(FuDevice *device, GError **error)
{
	FuLegionHid2ChildDevice *self = FU_LEGION_HID2_CHILD_DEVICE(device);
	g_autoptr(GByteArray) cmd = fu_struct_legion_get_pl_test_new();
	g_autoptr(GByteArray) tp_man = fu_struct_legion_get_pl_test_result_new();

	if (fu_device_get_proxy(FU_DEVICE(self)) == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	fu_struct_legion_get_pl_test_set_index(cmd, FU_LEGION_HID2_PL_TEST_TP_MANUFACTURER);

	if (!fu_legion_hid2_child_device_transfer(self, cmd, tp_man, error))
		return FALSE;

	self->manufacturer = fu_struct_legion_get_pl_test_result_get_content(tp_man);
	switch (self->manufacturer) {
	case FU_LEGION_HID2_TP_MAN_BETTER_LIFE:
		fu_device_set_vendor(device, "Better Life");
		fu_device_add_instance_strsafe(FU_DEVICE(self), "TP", "BL");
		break;
	case FU_LEGION_HID2_TP_MAN_SIPO:
		fu_device_set_vendor(device, "SIPO");
		fu_device_add_instance_strsafe(FU_DEVICE(self), "TP", "SIPO");
		break;
	default:
	case FU_LEGION_HID2_TP_MAN_NONE:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no touchpad found");
		return FALSE;
	}
	fu_device_build_instance_id(FU_DEVICE(self), NULL, "USB", "VID", "PID", "TP", NULL);

	return TRUE;
}

static gboolean
fu_legion_hid2_child_device_setup(FuDevice *device, GError **error)
{
	FuLegionHid2ChildDevice *self = FU_LEGION_HID2_CHILD_DEVICE(device);
	g_autoptr(GByteArray) cmd = fu_struct_legion_get_pl_test_new();
	g_autoptr(GByteArray) tp_ver = fu_struct_legion_get_pl_test_result_new();
	g_autofree gchar *version = NULL;

	if (fu_device_get_proxy(FU_DEVICE(self)) == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	fu_struct_legion_get_pl_test_set_index(cmd, FU_LEGION_HID2_PL_TEST_TP_VERSION);

	if (!fu_legion_hid2_child_device_transfer(self, cmd, tp_ver, error))
		return FALSE;

	version = g_strdup_printf("%d", fu_struct_legion_get_pl_test_result_get_content(tp_ver));
	fu_device_set_version(device, version);

	return TRUE;
}

static void
fu_legion_hid2_child_device_init(FuLegionHid2ChildDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "Touchpad");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FALLBACK);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.legion-hid2");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_logical_id(FU_DEVICE(self), "touchpad");
}

static void
fu_legion_hid2_child_device_class_init(FuLegionHid2ChildDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_legion_hid2_child_device_to_string;
	device_class->setup = fu_legion_hid2_child_device_setup;
	device_class->probe = fu_legion_hid2_child_device_probe;
}

FuDevice *
fu_legion_hid2_child_device_new(FuDevice *proxy)
{
	return g_object_new(FU_TYPE_LEGION_HID2_CHILD_DEVICE, "proxy", proxy, NULL);
}
