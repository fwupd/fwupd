/*
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-steelseries-fizz-hid.h"

#define STEELSERIES_BUFFER_REPORT_SIZE 64 + 1
#define STEELSERIES_REPORT_TIMEOUT     5000

#define STEELSERIES_HID_GET_REPORT 0x04U

#define STEELSERIES_HID_VERSION_COMMAND		 0x90U
#define STEELSERIES_HID_VERSION_REPORT_ID_OFFSET 0x00U
#define STEELSERIES_HID_VERSION_COMMAND_OFFSET	 0x01U
#define STEELSERIES_HID_VERSION_MODE_OFFSET	 0x02U

struct _FuSteelseriesFizzHid {
	FuUdevDevice parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesFizzHid, fu_steelseries_fizz_hid, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_steelseries_fizz_hid_command(FuDevice *device, guint8 *data, gsize datasz, GError **error)
{
	gboolean ret;

	ret = fu_udev_device_pwrite(FU_UDEV_DEVICE(device), 0, data, datasz, error);
	if (!ret) {
		g_prefix_error(error, "failed to write report: ");
		return FALSE;
	}

	/* cleanup the buffer before receiving any data */
	memset(data, 0x00, datasz);

	ret = fu_udev_device_pread(FU_UDEV_DEVICE(device), 0, data, datasz, error);
	if (!ret) {
		g_prefix_error(error, "failed to read report: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gchar *
fu_steelseries_fizz_hid_get_version(FuDevice *device, GError **error)
{
	guint8 data[STEELSERIES_BUFFER_REPORT_SIZE] = {0};
	const guint8 report_id = STEELSERIES_HID_GET_REPORT;
	const guint8 cmd = STEELSERIES_HID_VERSION_COMMAND;
	const guint8 mode = 0U; /* string */

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_HID_VERSION_REPORT_ID_OFFSET,
				    report_id,
				    error))
		return NULL;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_HID_VERSION_COMMAND_OFFSET,
				    cmd,
				    error))
		return NULL;

	if (!fu_memwrite_uint8_safe(data,
				    sizeof(data),
				    STEELSERIES_HID_VERSION_MODE_OFFSET,
				    mode,
				    error))
		return NULL;

	if (g_getenv("FWUPD_STEELSERIES_HID_VERBOSE") != NULL)
		fu_dump_raw(G_LOG_DOMAIN, "Version", data, sizeof(data));
	if (!fu_steelseries_fizz_hid_command(device, data, sizeof(data), error))
		return NULL;
	if (g_getenv("FWUPD_STEELSERIES_HID_VERBOSE") != NULL)
		fu_dump_raw(G_LOG_DOMAIN, "Version", data, sizeof(data));

	/* success */
	return fu_strsafe((const gchar *)&data[1], sizeof(data) - 1);
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
	g_autofree gchar *version = NULL;

	version = fu_steelseries_fizz_hid_get_version(device, error);
	if (version == NULL)
		return FALSE;
	fu_device_set_version(device, version);

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
