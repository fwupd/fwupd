/*
 * Copyright 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-steelseries-fizz-hid.h"
#include "fu-steelseries-fizz-struct.h"

#define FU_STEELSERIES_BUFFER_REPORT_SIZE 64 + 1

#define FU_STEELSERIES_HID_MAX_RETRIES 100

struct _FuSteelseriesFizzHid {
	FuUdevDevice parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesFizzHid, fu_steelseries_fizz_hid, FU_TYPE_UDEV_DEVICE)

typedef struct {
	GByteArray *buf_in;
	GByteArray *buf_out;
} FuSteelseriesFizzHidCommandHelper;

static gboolean
fu_steelseries_fizz_hid_command_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuSteelseriesFizzHidCommandHelper *helper = (FuSteelseriesFizzHidCommandHelper *)user_data;
	gboolean ret;
	guint8 buf[FU_STEELSERIES_BUFFER_REPORT_SIZE] = {0};
	g_autoptr(FuStructSteelseriesFizzHidResponse) st = NULL;
	g_autoptr(GError) error_local = NULL;

	/* force the request for each iteration to avoid a loop due the lost single packet --
	 * this is safe since the device doesn't support update over bluetooth */
	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    0, /* dst */
			    helper->buf_in->data,
			    helper->buf_in->len,
			    0, /* src */
			    helper->buf_in->len,
			    error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "write", buf, sizeof(buf));
	if (!fu_udev_device_pwrite(FU_UDEV_DEVICE(device), 0, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to write report: ");
		return FALSE;
	}

	memset(buf, 0x0, sizeof(buf));
	ret = fu_udev_device_pread(FU_UDEV_DEVICE(device), 0, buf, sizeof(buf), &error_local);
	st = fu_struct_steelseries_fizz_hid_response_parse(buf, sizeof(buf), 0x0, error);
	if (st == NULL)
		return FALSE;
	if (!ret) {
		/* since fu_udev_device_pread() treats unexpected data size as error
		 * we have to check the output additionally since the size of
		 * unexpected data size from mouse input data is only 16b */
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INTERNAL) ||
		    fu_struct_steelseries_fizz_hid_response_get_report_id(st) != 0x01) {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to read report: ");
			return FALSE;
		}
	}
	fu_dump_raw(G_LOG_DOMAIN, "read", buf, sizeof(buf));

	if (fu_struct_steelseries_fizz_hid_response_get_report_id(st) !=
	    FU_STRUCT_STEELSERIES_FIZZ_HID_GET_VERSION_REQ_DEFAULT_REPORT_ID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "data with unexpected Report ID (%u)",
			    fu_struct_steelseries_fizz_hid_response_get_report_id(st));
		return FALSE;
	}
	helper->buf_out = g_byte_array_new();
	g_byte_array_append(helper->buf_out, buf + 1, sizeof(buf) - 1);

	/* success */
	return TRUE;
}

static GByteArray *
fu_steelseries_fizz_hid_command(FuSteelseriesFizzHid *self, GByteArray *buf_in, GError **error)
{
	g_autoptr(GByteArray) buf_out = NULL;
	FuSteelseriesFizzHidCommandHelper helper = {
	    .buf_in = buf_in,
	    .buf_out = buf_out,
	};
	/* In BT mode the sync and data channels are sharing the device descriptor with the
	 * management channel.
	 * This is the reason why we receive "unexpected" packets with 0x01 or 0x05 Report IDs over
	 * the same descriptor on mouse connecting, waking up or just moving the mouse -- hence
	 * trying to repeat the query/response cycle lot of times */
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_steelseries_fizz_hid_command_cb,
				  FU_STEELSERIES_HID_MAX_RETRIES,
				  0, /* ms */
				  &helper,
				  error))
		return NULL;
	return g_steal_pointer(&buf_out);
}

static gboolean
fu_steelseries_fizz_hid_ensure_version(FuSteelseriesFizzHid *self, GError **error)
{
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) st_buf = NULL;
	g_autoptr(FuStructSteelseriesFizzHidGetVersionReq) st =
	    fu_struct_steelseries_fizz_hid_get_version_req_new();

	st_buf = fu_steelseries_fizz_hid_command(self, st, error);
	if (st_buf == NULL)
		return FALSE;
	version = fu_strsafe((const gchar *)st_buf->data, st_buf->len);
	if (version == NULL) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "unable to read version");
		return FALSE;
	}
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static gboolean
fu_steelseries_fizz_hid_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FwupdRequest) request = NULL;
	g_autofree gchar *msg = NULL;

	/* the user has to do something */
	msg = g_strdup_printf(
	    "%s needs to be manually connected either via the USB cable, "
	    "or via the 2.4G USB Wireless adapter to start the update. "
	    "Please plug either the USB-C cable and put the switch button underneath to off, "
	    "or the 2.4G USB Wireless adapter and put the switch button underneath to 2.4G.",
	    fu_device_get_name(device));
	request = fwupd_request_new();
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_PRESS_UNLOCK);
	fwupd_request_set_message(request, msg);
	if (!fu_device_emit_request(device, request, progress, error))
		return FALSE;

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_steelseries_fizz_hid_setup(FuDevice *device, GError **error)
{
	FuSteelseriesFizzHid *self = FU_STEELSERIES_FIZZ_HID(device);

	if (!fu_steelseries_fizz_hid_ensure_version(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_steelseries_fizz_hid_class_init(FuSteelseriesFizzHidClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->setup = fu_steelseries_fizz_hid_setup;
	device_class->detach = fu_steelseries_fizz_hid_detach;
}

static void
fu_steelseries_fizz_hid_init(FuSteelseriesFizzHid *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_NON_GENERIC_MESSAGE);
	fu_device_set_physical_id(FU_DEVICE(self), "hid");
	fu_device_add_protocol(FU_DEVICE(self), "com.steelseries.fizz");
	fu_device_set_remove_delay(FU_DEVICE(self), 300000); /* 5min */
}
