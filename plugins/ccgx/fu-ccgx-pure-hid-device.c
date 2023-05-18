/*
 * Copyright (C) 2023 Framework Computer Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-ccgx-common.h"
#include "fu-ccgx-firmware.h"
#include "fu-ccgx-pure-hid-device.h"

struct _FuCcgxPureHidDevice {
	FuHidDevice parent_instance;
	guint8 operating_mode;
	//guint8 bootloader_info;
	//guint8 bootmode_reason;
	//guint32 silicon_id;
	//bl_version: // [u8; 8],
	//image_1_ver: // [u8; 8],
	//image_2_ver: // [u8; 8],
	//guint32 image_1_row;
	//guint32 image_2_row;
	//device_uid: // [u8; 6],
};
G_DEFINE_TYPE(FuCcgxPureHidDevice, fu_ccgx_pure_hid_device, FU_TYPE_HID_DEVICE)


typedef struct __attribute__((packed)) {
	guint8 report_id;
	guint8 reserved_1;
	guint16 signature;
	guint8 operating_mode;
	guint8 bootloader_info;
	guint8 bootmode_reason;
	guint8 reserved_2;
	guint32 silicon_id;
	guint32 bl_version;
	guint8 bl_version_reserved[4];
	guint32 image_1_ver;
	guint8 image_1_ver_reserved[4];
	guint32 image_2_ver;
	guint8 image_2_ver_reserved[4];
	guint32 image_1_row;
	guint32 image_2_row;
	guint8 device_uid[6];
	guint8 reserved_3[10];
} HidFwInfo;

#define FU_CCGX_PURE_HID_DEVICE_TIMEOUT     5000 /* ms */
#define FU_CCGX_PURE_HID_DEVICE_RETRY_DELAY 30   /* ms */
#define FU_CCGX_PURE_HID_DEVICE_RETRY_CNT   5

static gboolean
fu_ccgx_pure_hid_flashing_mode(FuDevice *device, gpointer user_data, GError **error)
{
	guint8 buf[8] = {0xE1, 0x02, 0x50, 0x00, 0xCC, 0xCC, 0xCC, 0xCC};

	g_debug("before flashing mode");
	if (!fu_hid_device_set_report(FU_HID_DEVICE(device),
				      buf[0],
				      buf,
				      sizeof(buf),
				      FU_CCGX_PURE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "flashing enable command error: ");
		return FALSE;
	}
	g_debug("After flashing mode");
	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_magic_unlock(FuDevice *device, gpointer user_data, GError **error)
{
	guint8 magic_buf[8] = {0xE4, 0x42, 0x43, 0x59, 0x00, 0x00, 0x00, 0x0B};
	guint8 bridge_buf[8] = {0xE1, 0x06, 0x42, 0x00, 0xCC, 0xCC, 0xCC, 0xCC};
	gboolean err;

	if (!fu_hid_device_set_report(FU_HID_DEVICE(device),
				      magic_buf[0],
				      magic_buf,
				      sizeof(magic_buf),
				      FU_CCGX_PURE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		g_prefix_error(error, "magic enable command error: ");
		return FALSE;
	}


	err = fu_hid_device_set_report(FU_HID_DEVICE(device),
				      bridge_buf[0],
				      bridge_buf,
				      sizeof(bridge_buf),
				      FU_CCGX_PURE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      NULL);
	// Ignoring error
	err = err;

	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_get_fw_info(FuDevice *device, gpointer user_data, GError **error)
{
	FuCcgxPureHidDevice *self = FU_CCGX_PURE_HID_DEVICE(device);
	gsize bufsz = 0x40;
	g_autofree guint8 *buf = g_malloc0(bufsz);
	HidFwInfo *info;
	g_autofree gchar *ver1;
	g_autofree gchar *ver2;

	buf[0] = 0xE0;
	if (!fu_hid_device_get_report(FU_HID_DEVICE(device),
				      buf[0],
				      buf,
				      bufsz,
				      FU_CCGX_PURE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;

	fu_ccgx_pure_hid_flashing_mode(device, user_data, error);

	info = (HidFwInfo *)(void *)buf;

	self->operating_mode = info->operating_mode;
	g_debug("Report ID:       0x%02x", info->report_id);
	g_debug("Signature:       0x%02x", info->signature);
	g_debug("Operating Mode:  0x%02x", info->operating_mode);
	g_debug("Bootloader Info: 0x%02x", info->bootloader_info);
	g_debug("Bootmode Reason: 0x%02x", info->bootmode_reason);
	g_debug("Silicon ID:      0x%08x", info->silicon_id);
	g_debug("BL Version:      0x%08x", info->bl_version);
	//g_debug("Image 1 Ver:     0x%08x", info->image_1_ver);
	//g_debug("Image 2 Ver:     0x%08x", info->image_2_ver);
	ver1 = fu_ccgx_detailed_version_to_string(info->image_1_ver);
	fu_device_set_version(FU_DEVICE(device), ver1);
	fu_device_set_version(FU_DEVICE(device), ver1);
	fu_device_set_version_raw(FU_DEVICE(device), info->image_1_ver);
	g_debug("Image 1 Ver:     %s", ver1);
	ver2 = fu_ccgx_detailed_version_to_string(info->image_2_ver);
	g_debug("Image 2 Ver:     %s", ver2);
	g_debug("Image 1 Row:     0x%08x", info->image_1_row);
	g_debug("Image 2 Row:     0x%08x", info->image_2_row);
	g_debug("Device UID:      0x%012x", (guint32)info->device_uid[0]);

	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_get_fw_info_wrapper(FuDevice *device, gpointer user_data, GError **error)
{
	fu_ccgx_pure_hid_magic_unlock(device, user_data, error);
	return fu_ccgx_pure_hid_get_fw_info(device, user_data, error);
}

static gboolean
fu_ccgx_pure_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	//if (!fu_device_retry(device,
	//		     fu_ccgx_pure_hid_device_enable_hpi_mode_cb,
	//		     FU_CCGX_PURE_HID_DEVICE_RETRY_CNT,
	//		     NULL,
	//		     error))
	//	return FALSE;
	//fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_ccgx_pure_hid_device_setup(FuDevice *device, GError **error)
{
	g_debug("Setup Start");

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_ccgx_pure_hid_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_ccgx_pure_hid_get_fw_info_wrapper(device, NULL, error)) {
		return FALSE;
	}

	/* set summary */
	//summary = g_strdup_printf("CX%u USB audio device", self->chip_id);
	//fu_device_set_summary(device, summary);

	g_debug("Setup End");

	/* success */
	return TRUE;
}

//static void
//fu_ccgx_pure_hid_device_set_progress(FuDevice *self, FuProgress *progress)
//{
//	fu_progress_set_id(progress, G_STRLOC);
//	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
//	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
//	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
//	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
//	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
//}

static void
fu_ccgx_pure_hid_device_init(FuCcgxPureHidDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.cypress.ccgx");
	fu_device_add_protocol(FU_DEVICE(self), "com.infineon.ccgx");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
}

static void
fu_ccgx_pure_hid_device_class_init(FuCcgxPureHidDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->detach = fu_ccgx_pure_hid_device_detach;
	klass_device->setup = fu_ccgx_pure_hid_device_setup;
	//klass_device->set_progress = fu_ccgx_pure_hid_device_set_progress;
}
