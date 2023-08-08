/*
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-steelseries-fizz-hid.h"

#define STEELSERIES_BUFFER_REPORT_SIZE 64 + 1

#define STEELSERIES_HID_GET_REPORT 0x04U
#define STEELSERIES_HID_MAX_RETRIES 100

#define STEELSERIES_HID_VERSION_COMMAND		 0x90U
#define STEELSERIES_HID_VERSION_REPORT_ID_OFFSET 0x00U
#define STEELSERIES_HID_VERSION_COMMAND_OFFSET	 0x01U
#define STEELSERIES_HID_VERSION_MODE_OFFSET	 0x02U

struct _FuSteelseriesFizzHid {
	FuUdevDevice parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesFizzHid, fu_steelseries_fizz_hid, FU_TYPE_UDEV_DEVICE)

typedef struct {
	guint8 *buf;
	gsize bufsz;
} FuSteelseriesFizzHidCommandHelper;

static gboolean
fu_steelseries_fizz_hid_command_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuSteelseriesFizzHidCommandHelper *helper = (FuSteelseriesFizzHidCommandHelper *)user_data;
	gboolean ret;
	guint8 rdata[STEELSERIES_BUFFER_REPORT_SIZE] = {0};
	guint8 report_id = 0;
	g_autoptr(GError) error_local = NULL;

	/* force the request for each iteration to avoid a loop due the lost single packet --
	 * this is safe since the device doesn't support update over bluetooth */
	if (!fu_udev_device_pwrite(FU_UDEV_DEVICE(device), 0, helper->buf, helper->bufsz, error)) {
		g_prefix_error(error, "failed to write report: ");
		return FALSE;
	}

	ret = fu_udev_device_pread(FU_UDEV_DEVICE(device), 0, rdata, sizeof(rdata), &error_local);

	if (!fu_memread_uint8_safe(rdata,
				   sizeof(rdata),
				   STEELSERIES_HID_VERSION_REPORT_ID_OFFSET,
				   &report_id,
				   error))
		return FALSE;

	if (!ret) {
		/* since fu_udev_device_pread() treats unexpected data size as error
		 * we have to check the output additionally since the size of
		 * unexpected data size from mouse input data is only 16b */
		if (!g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_FAILED) ||
		    report_id != 0x01) {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to read report: ");
			return FALSE;
		}
	}

	fu_dump_raw(G_LOG_DOMAIN, "got report", rdata, sizeof(rdata));

	if (report_id != STEELSERIES_HID_GET_REPORT) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "data with unexpected Report ID (%u)",
			    report_id);
		return FALSE;
	}

	if (!fu_memcpy_safe(helper->buf,
			    helper->bufsz,
			    0,
			    rdata,
			    sizeof(rdata),
			    0,
			    helper->bufsz,
			    error)) {
		g_prefix_error(error, "failed to return data: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_hid_command(FuDevice *device, guint8 *data, gsize datasz, GError **error)
{
	FuSteelseriesFizzHidCommandHelper helper = {
	    .buf = data,
	    .bufsz = datasz,
	};
	/* In BT mode the sync and data channels are sharing the device descriptor with the
	 * management channel.
	 * This is the reason why we receive "unexpected" packets with 0x01 or 0x05 Report IDs over
	 * the same descriptor on mouse connecting, waking up or just moving the mouse -- hence
	 * trying to repeat the query/response cycle lot of times */
	return fu_device_retry_full(device,
				    fu_steelseries_fizz_hid_command_cb,
				    STEELSERIES_HID_MAX_RETRIES,
				    0, /* ms */
				    &helper,
				    error);
}

static gboolean
fu_steelseries_fizz_hid_ensure_version(FuDevice *device, GError **error)
{
	guint8 data[STEELSERIES_BUFFER_REPORT_SIZE] = {0};
	const guint8 report_id = STEELSERIES_HID_GET_REPORT;
	const guint8 cmd = STEELSERIES_HID_VERSION_COMMAND;
	const guint8 mode = 0U; /* string */
	g_autofree gchar *version = NULL;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_HID_VERSION_REPORT_ID_OFFSET,
				    report_id,
				    error))
		return FALSE;
	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_HID_VERSION_COMMAND_OFFSET,
				    cmd,
				    error))
		return FALSE;
	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_HID_VERSION_MODE_OFFSET,
				    mode,
				    error))
		return FALSE;

	fu_dump_raw(G_LOG_DOMAIN, "Version", data, sizeof(data));
	if (!fu_steelseries_fizz_hid_command(device, data, sizeof(data), error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "Version", data, sizeof(data));

	/* success */
	version = fu_memstrsafe(data, sizeof(data), 0x1, sizeof(data) - 1, error);
	if (version == NULL) {
		g_prefix_error(error, "unable to read version: ");
		return FALSE;
	}
	fu_device_set_version(device, version);
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
	fu_device_emit_request(device, request);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_fizz_hid_setup(FuDevice *device, GError **error)
{
	if (!fu_steelseries_fizz_hid_ensure_version(device, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_steelseries_fizz_hid_class_init(FuSteelseriesFizzHidClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	klass_device->setup = fu_steelseries_fizz_hid_setup;
	klass_device->detach = fu_steelseries_fizz_hid_detach;
}

static void
fu_steelseries_fizz_hid_init(FuSteelseriesFizzHid *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_physical_id(FU_DEVICE(self), "hid");
	fu_device_add_protocol(FU_DEVICE(self), "com.steelseries.fizz");
	fu_device_set_remove_delay(FU_DEVICE(self), 300000); /* 5min */
}
