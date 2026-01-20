/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-sunwinon-hid-device.h"
#include "fu-sunwinon-util-dfu-master.h"

struct _FuSunwinonHidDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuSunwinonHidDevice, fu_sunwinon_hid_device, FU_TYPE_HIDRAW_DEVICE)

static void
fu_sunwinon_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-firmware");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_sunwinon_hid_device_fetch_fw_version_2(FuSunwinonHidDevice *device, GError **error)
{
	FuSunwinonDfuImageInfo fw_info = {0};
	g_autoptr(FuSwDfuMaster) dfu_master = NULL;

	dfu_master = fu_sunwinon_util_dfu_master_2_new(NULL, 0, FU_DEVICE(device), error);
	if (!fu_sunwinon_util_dfu_master_2_fetch_fw_version(dfu_master, &fw_info, error))
		return FALSE;
	g_debug("firmware version fetched: %u.%u",
		(guint)((fw_info.version >> 8) & 0xFF),
		(guint)(fw_info.version & 0xFF));
	fu_device_set_version(FU_DEVICE(device),
			      g_strdup_printf("%u.%u",
					      (guint)((fw_info.version >> 8) & 0xFF),
					      (guint)(fw_info.version & 0xFF)));
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_check_update_channel(FuHidDescriptor *desc, GError **error)
{
	g_autoptr(FuHidReport) report_out = NULL;
	g_autoptr(FuHidReport) report_in = NULL;

	g_return_val_if_fail(desc != NULL, FALSE);

	report_out = fu_hid_descriptor_find_report(desc,
						   error,
						   "report-id",
						   FU_SUNWINON_HID_REPORT_CHANNEL_ID,
						   "usage",
						   0x01,
						   "output",
						   0x02,
						   NULL);
	if (report_out == NULL)
		return FALSE;

	report_in = fu_hid_descriptor_find_report(desc,
						  error,
						  "report-id",
						  FU_SUNWINON_HID_REPORT_CHANNEL_ID,
						  "usage",
						  0x01,
						  "input",
						  0x02,
						  NULL);
	if (report_in == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_setup(FuDevice *device, GError **error)
{
	g_autoptr(FuHidDescriptor) descriptor =
	    fu_hidraw_device_parse_descriptor(FU_HIDRAW_DEVICE(device), error);
	if (descriptor == NULL)
		return FALSE;
	g_debug("HID descriptor parsed successfully");
	if (!fu_sunwinon_hid_device_check_update_channel(descriptor, error))
		return FALSE;
	if (!fu_sunwinon_hid_device_fetch_fw_version_2(FU_SUNWINON_HID_DEVICE(device), error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_probe(FuDevice *device, GError **error)
{
	fu_device_add_instance_id(device, "SUNWINON_HID");
	return TRUE;
}

static void
fu_sunwinon_hid_device_init(FuSunwinonHidDevice *self)
{
	g_debug("initializing sunwinon HID device");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TABLET);
	fu_device_set_id(FU_DEVICE(self), "SunwinonHidTest");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_protocol(FU_DEVICE(self), "com.sunwinon.hid");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

static gboolean
fu_sunwinon_hid_device_write_firmware_2(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	gconstpointer fw = NULL;
	gsize fw_sz = 0;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuSwDfuMaster) dfu_master = NULL;

	(void)flags;

	locker = fu_device_locker_new(device, error);
	g_return_val_if_fail(locker != NULL, FALSE);

	blob = fu_firmware_get_bytes(firmware, error);
	g_return_val_if_fail(blob != NULL, FALSE);

	fw = g_bytes_get_data(blob, &fw_sz);
	dfu_master = fu_sunwinon_util_dfu_master_2_new(fw, fw_sz, device, error);
	if (dfu_master == NULL)
		return FALSE;

	if (!fu_sunwinon_util_dfu_master_2_write_firmware(dfu_master,
							  progress,
							  FU_SUNWINON_FAST_DFU_MODE_DISABLE,
							  FU_SUNWINON_DFU_UPGRADE_MODE_COPY,
							  error))
		return FALSE;
	return TRUE;
}

static void
fu_sunwinon_hid_device_class_init(FuSunwinonHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_sunwinon_hid_device_probe;
	device_class->setup = fu_sunwinon_hid_device_setup;
	device_class->write_firmware = fu_sunwinon_hid_device_write_firmware_2;
	device_class->set_progress = fu_sunwinon_hid_device_set_progress;
}
