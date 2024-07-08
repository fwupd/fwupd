/*
 * Copyright 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-genesys-common.h"
#include "fu-genesys-hubhid-device.h"

#define GENESYS_HUBHID_REPORT_ID	  0
#define GENESYS_HUBHID_REPORT_BYTE_LENGTH 0x40
#define GENESYS_HUBHID_REPORT_TIMEOUT	  100 /* ms */
#define GENESYS_HUBHID_REPORT_FLAGS	  FU_HID_DEVICE_FLAG_ALLOW_TRUNC | FU_HID_DEVICE_FLAG_IS_FEATURE

typedef union {
	struct {
		guint8 recipient : 2; /* FuUsbRecipient */
		guint8 reserved : 3;
		guint8 type : 2; /* FuUsbRequestType */
		guint8 dir : 1;	 /* FuUsbDirection */
	};
	guint8 bm;
} FuGenesysUsbRequestType;

typedef struct {
	FuGenesysUsbRequestType req_type;
	guint8 request;
	guint16 value;
	guint16 index;
	guint16 length;
} FuGenesysUsbSetup;

struct _FuGenesysHubhidDevice {
	FuHidDevice parent_instance;

	gboolean support_report_pack;
	guint16 report_length;
	guint16 max_report_pack_data_length;
};

G_DEFINE_TYPE(FuGenesysHubhidDevice, fu_genesys_hubhid_device, FU_TYPE_HID_DEVICE)

static gboolean
fu_genesys_hubhid_device_command_read(FuGenesysHubhidDevice *self,
				      FuGenesysUsbSetup *setup,
				      guint8 *data,
				      gsize datasz,
				      FuProgress *progress,
				      GError **error)
{
	FuHidDevice *hid_device = FU_HID_DEVICE(self);
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GByteArray) buf_report = g_byte_array_new();

	g_return_val_if_fail(datasz == setup->length, FALSE);

	fu_byte_array_set_size(buf_report, self->report_length, 0);

	/* set request data */
	if (!fu_memcpy_safe(buf_report->data,
			    buf_report->len,
			    0, /* dst */
			    (const guint8 *)setup,
			    sizeof(FuGenesysUsbSetup),
			    0x0, /* src */
			    sizeof(FuGenesysUsbSetup),
			    error))
		return FALSE;

	/* send request report */
	if (!fu_hid_device_set_report(hid_device,
				      GENESYS_HUBHID_REPORT_ID,
				      buf_report->data,
				      buf_report->len,
				      GENESYS_HUBHID_REPORT_TIMEOUT,
				      GENESYS_HUBHID_REPORT_FLAGS,
				      error))
		return FALSE;

	if (setup->length == 0) {
		g_warning("read zero-length hid report");
		return TRUE;
	}

	/* receive report */
	chunks = fu_chunk_array_mutable_new(data, setup->length, 0, 0x0, buf_report->len);
	if (progress != NULL) {
		fu_progress_set_id(progress, G_STRLOC);
		fu_progress_set_steps(progress, chunks->len);
	}
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		memset(buf_report->data, 0, buf_report->len);
		if (!fu_hid_device_get_report(hid_device,
					      GENESYS_HUBHID_REPORT_ID,
					      buf_report->data,
					      buf_report->len,
					      GENESYS_HUBHID_REPORT_TIMEOUT,
					      GENESYS_HUBHID_REPORT_FLAGS |
						  FU_HID_DEVICE_FLAG_RETRY_FAILURE,
					      error)) {
			g_prefix_error(error,
				       "error getting report at 0x%04x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		if (!fu_memcpy_safe(fu_chunk_get_data_out(chk),
				    fu_chunk_get_data_sz(chk),
				    0, /* dst */
				    buf_report->data,
				    buf_report->len,
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error)) {
			g_prefix_error(error,
				       "error getting report data at 0x%04x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		if (progress != NULL)
			fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_hubhid_device_can_pack_report(FuGenesysHubhidDevice *self, guint16 data_length)
{
	if (!self->support_report_pack)
		return FALSE;

	if (data_length > self->max_report_pack_data_length)
		return FALSE;

	return TRUE;
}

static gboolean
fu_genesys_hubhid_device_command_write(FuGenesysHubhidDevice *self,
				       FuGenesysUsbSetup *setup,
				       const guint8 *data,
				       gsize datasz,
				       FuProgress *progress,
				       GError **error)
{
	FuHidDevice *hid_device = FU_HID_DEVICE(self);
	gboolean pack_report = FALSE;
	g_autoptr(GByteArray) buf_report = g_byte_array_new();

	g_return_val_if_fail(datasz == setup->length, FALSE);

	fu_byte_array_set_size(buf_report, self->report_length, 0);

	/* set request data */
	if (!fu_memcpy_safe(buf_report->data,
			    buf_report->len,
			    0, /* dst */
			    (const guint8 *)setup,
			    sizeof(FuGenesysUsbSetup),
			    0x0, /* src */
			    sizeof(FuGenesysUsbSetup),
			    error))
		return FALSE;

	/* pack report if it can */
	pack_report = fu_genesys_hubhid_device_can_pack_report(self, setup->length);
	if (pack_report) {
		if (setup->length > 0 && !fu_memcpy_safe(buf_report->data,
							 buf_report->len,
							 sizeof(FuGenesysUsbSetup), /* dst */
							 data,
							 datasz,
							 0x0, /* src */
							 setup->length,
							 error)) {
			g_prefix_error(error, "error packing request data: ");
			return FALSE;
		}
	}

	/* send request report */
	if (!fu_hid_device_set_report(hid_device,
				      GENESYS_HUBHID_REPORT_ID,
				      buf_report->data,
				      buf_report->len,
				      GENESYS_HUBHID_REPORT_TIMEOUT,
				      GENESYS_HUBHID_REPORT_FLAGS,
				      error))
		return FALSE;

	/* command completed after packed report sent */
	if (pack_report)
		return TRUE;

	/* send report */
	if (setup->length > 0) {
		g_autoptr(GPtrArray) chunks =
		    fu_chunk_array_new(data, setup->length, 0, 0, buf_report->len);
		if (progress != NULL) {
			fu_progress_set_id(progress, G_STRLOC);
			fu_progress_set_steps(progress, chunks->len);
		}
		for (guint i = 0; i < chunks->len; i++) {
			FuChunk *chk = g_ptr_array_index(chunks, i);

			memset(buf_report->data, 0, buf_report->len);
			if (!fu_memcpy_safe(buf_report->data,
					    buf_report->len,
					    0, /* dst */
					    fu_chunk_get_data(chk),
					    fu_chunk_get_data_sz(chk),
					    0x0, /* src */
					    fu_chunk_get_data_sz(chk),
					    error)) {
				g_prefix_error(error,
					       "error setting report data at 0x%04x: ",
					       (guint)fu_chunk_get_address(chk));
				return FALSE;
			}
			if (!fu_hid_device_set_report(hid_device,
						      GENESYS_HUBHID_REPORT_ID,
						      buf_report->data,
						      buf_report->len,
						      GENESYS_HUBHID_REPORT_TIMEOUT,
						      GENESYS_HUBHID_REPORT_FLAGS |
							  FU_HID_DEVICE_FLAG_RETRY_FAILURE,
						      error)) {
				g_prefix_error(error,
					       "error setting report at 0x%04x: ",
					       (guint)fu_chunk_get_address(chk));
				return FALSE;
			}
			if (progress != NULL)
				fu_progress_step_done(progress);
		}
	}

	/* finish report */
	if (!fu_hid_device_get_report(hid_device,
				      GENESYS_HUBHID_REPORT_ID,
				      buf_report->data,
				      buf_report->len,
				      GENESYS_HUBHID_REPORT_TIMEOUT,
				      GENESYS_HUBHID_REPORT_FLAGS,
				      error)) {
		g_prefix_error(error, "error finishing report: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_genesys_hubhid_device_send_report(FuGenesysHubhidDevice *self,
				     FuProgress *progress,
				     FuUsbDirection direction,
				     FuUsbRequestType request_type,
				     FuUsbRecipient recipient,
				     guint8 request,
				     guint16 value,
				     guint16 idx,
				     guint8 *data,
				     gsize datasz,
				     GError **error)
{
	g_autofree FuGenesysUsbSetup *setup = g_new0(FuGenesysUsbSetup, 1);

	setup->req_type.dir = (direction == FU_USB_DIRECTION_DEVICE_TO_HOST)
				  ? 1
				  : 0; /* revert fu_usb in/out dir to usb spec */
	setup->req_type.type = request_type;
	setup->req_type.recipient = recipient;
	setup->request = request;
	setup->value = value;
	setup->index = idx;
	setup->length = datasz;

	if (direction == FU_USB_DIRECTION_DEVICE_TO_HOST) {
		return fu_genesys_hubhid_device_command_read(self,
							     setup,
							     data,
							     datasz,
							     progress,
							     error);
	} else {
		return fu_genesys_hubhid_device_command_write(self,
							      setup,
							      data,
							      datasz,
							      progress,
							      error);
	}
}

static gboolean
fu_genesys_hubhid_device_validate_token(FuGenesysHubhidDevice *self, GError **error)
{
	g_autoptr(GByteArray) buf_hid_token = NULL;
	g_autoptr(GByteArray) buf_data = g_byte_array_new();
	g_autofree FuGenesysUsbSetup *setup = g_new0(FuGenesysUsbSetup, 1);

	buf_hid_token = fu_utf8_to_utf16_byte_array("GLI HID",
						    G_LITTLE_ENDIAN,
						    FU_UTF_CONVERT_FLAG_NONE,
						    error);
	if (buf_hid_token == NULL)
		return FALSE;

	/* get 0x80 string descriptor */
	setup->req_type.bm = 0x80;
	setup->request = 0x06;
	setup->value = (0x03 << 8) | 0x80;
	setup->index = 0;
	setup->length = 0x40;

	fu_byte_array_set_size(buf_data, setup->length, 0);

	if (!fu_genesys_hubhid_device_command_read(self,
						   setup,
						   buf_data->data,
						   buf_data->len,
						   NULL,
						   error))
		return FALSE;
	if (!fu_memcmp_safe(buf_data->data,
			    buf_data->len,
			    0x2,
			    buf_hid_token->data,
			    buf_hid_token->len,
			    0,
			    buf_hid_token->len,
			    error)) {
		g_prefix_error(error, "wrong HID token string: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_hubhid_device_probe(FuDevice *device, GError **error)
{
	if (fu_usb_device_get_class(FU_USB_DEVICE(device)) != FU_USB_CLASS_INTERFACE_DESC) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "is not a hub hid");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_hubhid_device_setup(FuDevice *device, GError **error)
{
	FuGenesysHubhidDevice *self = FU_GENESYS_HUBHID_DEVICE(device);

	/* validate by string token */
	if (!fu_genesys_hubhid_device_validate_token(self, error))
		return FALSE;

	/* FuHidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_genesys_hubhid_device_parent_class)->setup(device, error)) {
		g_prefix_error(error, "error setting up device: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_genesys_hubhid_device_init(FuGenesysHubhidDevice *self)
{
	self->support_report_pack = TRUE;
	self->report_length = GENESYS_HUBHID_REPORT_BYTE_LENGTH;
	self->max_report_pack_data_length = self->report_length - sizeof(FuGenesysUsbSetup);
}

static void
fu_genesys_hubhid_device_class_init(FuGenesysHubhidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_genesys_hubhid_device_probe;
	device_class->setup = fu_genesys_hubhid_device_setup;
}
