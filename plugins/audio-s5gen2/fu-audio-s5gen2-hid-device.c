/*
 * Copyright 2023 Denis Pynkin <denis.pynkin@collabora.com>
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-audio-s5gen2-device.h"
#include "fu-audio-s5gen2-hid-device.h"
#include "fu-audio-s5gen2-hid-struct.h"
#include "fu-audio-s5gen2-impl.h"
#include "fu-audio-s5gen2-struct.h"

#define HID_IFACE  0x01
#define HID_EP_IN  0x82
#define HID_EP_OUT 0x01

#define FU_QC_S5GEN2_HID_DEVICE_TIMEOUT 0 /* ms */

#define FU_QC_S5GEN2_HID_DEVICE_MAX_TRANSFER_SIZE 255

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
fu_qc_s5gen2_hid_device_msg_in(FuQcS5gen2Impl *impl,
			       guint8 *data,
			       gsize data_len,
			       gsize *read_len,
			       GError **error)
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

	*read_len = fu_struct_qc_hid_response_get_payload_len(msg);

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
fu_qc_s5gen2_hid_device_cmd_req_disconnect(FuQcS5gen2Impl *impl, GError **error)
{
	g_autoptr(GByteArray) req = fu_struct_qc_disconnect_req_new();
	return fu_qc_s5gen2_hid_device_msg_cmd(impl, req->data, req->len, error);
}

static gboolean
fu_qc_s5gen2_hid_device_cmd_req_connect(FuQcS5gen2Impl *impl, GError **error)
{
	guint8 data_in[FU_STRUCT_QC_UPDATE_STATUS_SIZE] = {0x0};
	gsize read_len;
	FuQcStatus update_status;
	g_autoptr(GByteArray) req = fu_struct_qc_connect_req_new();
	g_autoptr(GByteArray) st = NULL;

	if (!fu_qc_s5gen2_hid_device_msg_cmd(impl, req->data, req->len, error))
		return FALSE;
	if (!fu_qc_s5gen2_hid_device_msg_in(impl, data_in, sizeof(data_in), &read_len, error))
		return FALSE;
	st = fu_struct_qc_update_status_parse(data_in, read_len, 0, error);
	if (st == NULL)
		return FALSE;

	update_status = fu_struct_qc_update_status_get_status(st);
	switch (update_status) {
	case FU_QC_STATUS_SUCCESS:
		break;
	case FU_QC_STATUS_ALREADY_CONNECTED_WARNING:
		g_debug("device is already connected");
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid update status (%s)",
			    fu_qc_status_to_string(update_status));
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_qc_s5gen2_hid_device_data_size(FuQcS5gen2Impl *impl, gsize *data_sz, GError **error)
{
	if (FU_QC_S5GEN2_HID_DEVICE_MAX_TRANSFER_SIZE <= FU_STRUCT_QC_DATA_SIZE + 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "MTU is not sufficient");
		return FALSE;
	}

	*data_sz = FU_QC_S5GEN2_HID_DEVICE_MAX_TRANSFER_SIZE - FU_STRUCT_QC_DATA_SIZE - 2;
	return TRUE;
}

static gboolean
fu_qc_s5gen2_hid_device_probe(FuDevice *device, GError **error)
{
	FuHidDevice *hid_device = FU_HID_DEVICE(device);
	FuUsbInterface *iface = NULL;
	g_autoptr(GPtrArray) ifaces = NULL;

	ifaces = fu_usb_device_get_interfaces(FU_USB_DEVICE(device), error);
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
	if (fu_usb_interface_get_class(iface) != FU_USB_CLASS_HID) {
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
	iface->req_connect = fu_qc_s5gen2_hid_device_cmd_req_connect;
	iface->req_disconnect = fu_qc_s5gen2_hid_device_cmd_req_disconnect;
	iface->data_size = fu_qc_s5gen2_hid_device_data_size;
}

static void
fu_qc_s5gen2_hid_device_class_init(FuQcS5gen2HidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_qc_s5gen2_hid_device_probe;
}
