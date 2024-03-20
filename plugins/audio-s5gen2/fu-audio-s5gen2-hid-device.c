/*
 * Copyright (C) 2023 Denis Pynkin <denis.pynkin@collabora.com>
 * Copyright (C) 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-audio-s5gen2-device.h"
#include "fu-audio-s5gen2-hid-device.h"
#include "fu-audio-s5gen2-hid-struct.h"
#include "fu-audio-s5gen2-impl.h"

#define HID_IFACE  0x01
#define HID_EP_IN  0x82
#define HID_EP_OUT 0x01

/* FIXME: value :-| */
#define FU_QC_S5GEN2_HID_DEVICE_TIMEOUT 0 /* ms */

struct _FuQcS5gen2HidDevice {
	FuHidDevice parent_instance;
};

static void
fu_qc_s5gen2_hid_device_impl_iface_init(FuQcS5gen2ImplInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuQcS5gen2HidDevice,
			fu_qc_s5gen2_hid_device,
			FU_TYPE_HID_DEVICE,
			G_IMPLEMENT_INTERFACE(FU_TYPE_QC_S5GEN2_IMPL,
					      fu_qc_s5gen2_hid_device_impl_iface_init))

static gboolean
fu_qc_s5gen2_hid_device_msg_out(FuQcS5gen2Impl *impl, guint8 *data, gsize data_len, GError **error)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(impl);
	g_autoptr(GByteArray) msg = fu_struct_qc_hid_data_transfer_new();

	fu_struct_qc_hid_data_transfer_set_payload_len(msg, data_len);
	if (!fu_struct_qc_hid_data_transfer_set_payload(msg, data, data_len, error))
		return FALSE;

	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					0x00,
					msg->data,
					FU_STRUCT_QC_HID_DATA_TRANSFER_SIZE,
					FU_QC_S5GEN2_HID_DEVICE_TIMEOUT,
					FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					error);
}

static gboolean
fu_qc_s5gen2_hid_device_msg_in(FuQcS5gen2Impl *impl, guint8 *data, gsize data_len, GError **error)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(impl);
	guint8 buf[FU_STRUCT_QC_HID_RESPONSE_SIZE] = {0x0};
	g_autoptr(GByteArray) msg = NULL;

	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      0x00,
				      buf,
				      FU_STRUCT_QC_HID_RESPONSE_SIZE,
				      FU_QC_S5GEN2_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
				      error))
		return FALSE;

	msg = fu_struct_qc_hid_response_parse(buf, FU_STRUCT_QC_HID_RESPONSE_SIZE, 0, error);
	if (msg == NULL)
		return FALSE;

	if (!fu_memcpy_safe(data,
			    data_len,
			    0,
			    msg->data,
			    msg->len,
			    FU_STRUCT_QC_HID_RESPONSE_OFFSET_PAYLOAD,
			    fu_struct_qc_hid_response_get_payload_len(msg),
			    error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_hid_device_msg_cmd(FuQcS5gen2Impl *impl, guint8 *data, gsize data_len, GError **error)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(impl);
	g_autoptr(GByteArray) msg = fu_struct_qc_hid_command_new();

	fu_struct_qc_hid_command_set_payload_len(msg, data_len);
	if (!fu_struct_qc_hid_command_set_payload(msg, data, data_len, error))
		return FALSE;

	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					0x03,
					msg->data,
					FU_STRUCT_QC_HID_COMMAND_SIZE,
					0,
					FU_HID_DEVICE_FLAG_IS_FEATURE,
					error);
}

static gboolean
fu_qc_s5gen2_hid_device_probe(FuDevice *device, GError **error)
{
	FuHidDevice *hid_device = FU_HID_DEVICE(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	GUsbInterface *iface = NULL;
	g_autoptr(GPtrArray) ifaces = NULL;

	ifaces = g_usb_device_get_interfaces(usb_device, error);
	if (ifaces == NULL)
		return FALSE;

	/* need the second HID interface */
	if (ifaces->len <= HID_IFACE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "transitional device detected");
		return FALSE;
	}

	iface = g_ptr_array_index(ifaces, HID_IFACE);
	if (g_usb_interface_get_class(iface) != G_USB_DEVICE_CLASS_HID) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "target interface is not HID");
		return FALSE;
	}

	fu_hid_device_set_interface(hid_device, HID_IFACE);
	fu_hid_device_set_ep_addr_in(hid_device, HID_EP_IN);
	fu_hid_device_set_ep_addr_out(hid_device, HID_EP_OUT);

	/* FuHidDevice->probe */
	if (!FU_DEVICE_CLASS(fu_qc_s5gen2_hid_device_parent_class)->probe(device, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_qc_s5gen2_hid_device_init(FuQcS5gen2HidDevice *self)
{
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_RETRY_FAILURE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_QC_S5GEN2_DEVICE_REMOVE_DELAY);
	fu_device_set_battery_threshold(FU_DEVICE(self), 0);
}

static void
fu_qc_s5gen2_hid_device_impl_iface_init(FuQcS5gen2ImplInterface *iface)
{
	iface->msg_in = fu_qc_s5gen2_hid_device_msg_in;
	iface->msg_out = fu_qc_s5gen2_hid_device_msg_out;
	iface->msg_cmd = fu_qc_s5gen2_hid_device_msg_cmd;
}

static void
fu_qc_s5gen2_hid_device_class_init(FuQcS5gen2HidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_qc_s5gen2_hid_device_probe;
}
