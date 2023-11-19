/*
 * Copyright (C) 2023 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-audio-s5gen2-device.h"
#include "fu-audio-s5gen2-hid-device.h"
#include "fu-audio-s5gen2-hid-struct.h"
#include "fu-audio-s5gen2-struct.h"

#define HID_IFACE  0x01
#define HID_EP_IN  0x82
#define HID_EP_OUT 0x01

/* FIXME: values :-| */
#define FU_QC_S5GEN2_HID_DEVICE_TIMEOUT	     0	   /* ms */
#define FU_QC_S5GEN2_HID_DEVICE_REMOVE_DELAY 60000 /* ms */

struct _FuQcS5gen2HidDevice {
	FuHidDevice parent_instance;
	FuDevice *core;
};

G_DEFINE_TYPE(FuQcS5gen2HidDevice, fu_qc_s5gen2_hid_device, FU_TYPE_HID_DEVICE)

static gboolean
fu_qc_s5gen2_hid_device_msg_out(FuDevice *device, guint8 *data, gsize data_len, GError **error)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(device);
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
fu_qc_s5gen2_hid_device_msg_in(FuDevice *device, guint8 *data_in, gsize data_len, GError **error)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(device);
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

	if (!fu_memcpy_safe(data_in,
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
fu_qc_s5gen2_hid_device_msg_cmd(FuDevice *device, guint8 *data, gsize data_len, GError **error)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(device);
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

static gboolean
fu_qc_s5gen2_hid_device_setup(FuDevice *device, GError **error)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(device);
	FuQcS5gen2DeviceClass *klass_core;

	/* HidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_qc_s5gen2_hid_device_parent_class)->setup(device, error))
		return FALSE;

	self->core = fu_qc_s5gen2_device_new(device);
	if (self->core == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "unable to create the core");
		return FALSE;
	}
	klass_core = FU_QC_S5GEN2_DEVICE_GET_CLASS(self->core);
	klass_core->msg_out = fu_qc_s5gen2_hid_device_msg_out;
	klass_core->msg_in = fu_qc_s5gen2_hid_device_msg_in;
	klass_core->msg_cmd = fu_qc_s5gen2_hid_device_msg_cmd;

	if (!FU_DEVICE_CLASS(klass_core)->setup(self->core, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_qc_s5gen2_hid_device_reload(FuDevice *device, GError **error)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(device);
	FuQcS5gen2DeviceClass *klass_core = FU_QC_S5GEN2_DEVICE_GET_CLASS(self->core);
	return FU_DEVICE_CLASS(klass_core)->reload(self->core, error);
}

static gboolean
fu_qc_s5gen2_hid_device_prepare(FuDevice *device,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(device);
	FuQcS5gen2DeviceClass *klass_core = FU_QC_S5GEN2_DEVICE_GET_CLASS(self->core);
	return FU_DEVICE_CLASS(klass_core)->prepare(self->core, progress, flags, error);
}

static gboolean
fu_qc_s5gen2_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(device);
	FuQcS5gen2DeviceClass *klass_core = FU_QC_S5GEN2_DEVICE_GET_CLASS(self->core);
	return FU_DEVICE_CLASS(klass_core)->attach(self->core, progress, error);
}

static FuFirmware *
fu_qc_s5gen2_hid_device_prepare_firmware(FuDevice *device,
					 GInputStream *stream,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(device);
	FuQcS5gen2DeviceClass *klass_core = FU_QC_S5GEN2_DEVICE_GET_CLASS(self->core);
	return FU_DEVICE_CLASS(klass_core)->prepare_firmware(self->core, stream, flags, error);
}

static gboolean
fu_qc_s5gen2_hid_device_write_firmware(FuDevice *device,
				       FuFirmware *firmware,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(device);
	FuQcS5gen2DeviceClass *klass_core = FU_QC_S5GEN2_DEVICE_GET_CLASS(self->core);
	if (!FU_DEVICE_CLASS(klass_core)
		 ->write_firmware(self->core, firmware, progress, flags, error))
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_qc_s5gen2_hid_device_replace(FuDevice *device, FuDevice *donor)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(device);
	FuQcS5gen2HidDevice *prev = FU_QC_S5GEN2_HID_DEVICE(donor);
	guint32 id = fu_qc_s5gen2_device_get_file_id(FU_QC_S5GEN2_DEVICE(prev->core));
	guint8 version = fu_qc_s5gen2_device_get_file_version(FU_QC_S5GEN2_DEVICE(prev->core));

	fu_qc_s5gen2_device_set_file_id(FU_QC_S5GEN2_DEVICE(self->core), id);
	fu_qc_s5gen2_device_set_file_version(FU_QC_S5GEN2_DEVICE(self->core), version);
}

static void
fu_qc_s5gen2_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_qc_s5gen2_hid_device_init(FuQcS5gen2HidDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_QC_S5GEN2_HID_DEVICE_REMOVE_DELAY);
	fu_device_add_protocol(FU_DEVICE(self), "com.qualcomm.s5gen2");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_RETRY_FAILURE);
	/* for USB-attached device */
	fu_device_set_battery_threshold(FU_DEVICE(self), 0);
}

static void
fu_qc_s5gen2_hid_device_finalize(GObject *object)
{
	FuQcS5gen2HidDevice *self = FU_QC_S5GEN2_HID_DEVICE(object);

	if (self->core != NULL)
		g_object_unref(self->core);

	G_OBJECT_CLASS(fu_qc_s5gen2_hid_device_parent_class)->finalize(object);
}

static void
fu_qc_s5gen2_hid_device_class_init(FuQcS5gen2HidDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_qc_s5gen2_hid_device_finalize;
	klass_device->probe = fu_qc_s5gen2_hid_device_probe;
	klass_device->setup = fu_qc_s5gen2_hid_device_setup;
	klass_device->reload = fu_qc_s5gen2_hid_device_reload;
	klass_device->prepare = fu_qc_s5gen2_hid_device_prepare;
	klass_device->attach = fu_qc_s5gen2_hid_device_attach;
	klass_device->prepare_firmware = fu_qc_s5gen2_hid_device_prepare_firmware;
	klass_device->write_firmware = fu_qc_s5gen2_hid_device_write_firmware;
	klass_device->set_progress = fu_qc_s5gen2_hid_device_set_progress;
	klass_device->replace = fu_qc_s5gen2_hid_device_replace;
}
