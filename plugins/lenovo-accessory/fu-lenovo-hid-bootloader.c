/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-command.h"
#include "fu-lenovo-hid-bootloader.h"

struct _FuLenovoHidBootloader {
	FuHidrawDevice parent_instance;
};
G_DEFINE_TYPE(FuLenovoHidBootloader, fu_lenovo_hid_bootloader, FU_TYPE_HIDRAW_DEVICE)

static guint32
fu_lenovo_hid_bootloader_crc32_compute(guint8 const *p_data, guint32 size, guint32 const *p_crc)
/*++

Function Description:
  Feed each consecutive data block into this function, along with the current value of p_crc as
  returned by the previous call of this function. The first call of this function should pass NULL
   as the initial value of the crc in p_crc.

Arguments:
  *p_data = The input data block for computation.
  size    = The size of the input data block in bytes.
  *p_crc  = The previous calculated CRC-32 value or NULL if first call.

Returns:
  The updated CRC-32 value, based on the input supplied

--*/
{
	guint32 crc;

	crc = (p_crc == NULL) ? 0xFFFFFFFF : ~(*p_crc);
	for (guint32 i = 0; i < size; i++) {
		crc = crc ^ p_data[i];
		for (guint32 j = 8; j > 0; j--) {
			crc = (crc >> 1) ^ (0xEDB88320U & ((crc & 1) ? 0xFFFFFFFF : 0));
		}
	}

	return ~crc;
}
/*name comment to ignore*/
static gboolean
fu_lenovo_hid_bootloader_write_file_data(FuLenovoHidBootloader *self,
					 guint8 file_type,
					 gconstpointer file_data,
					 guint32 file_size,
					 guint8 block_size,
					 FuProgress *progress,
					 GError **error)
{
	guint32 done = 0;
	while (done < file_size) {
		guint32 chunk = MIN(block_size, file_size - done);
		/* 去 const：拷一块可写内存 */
		g_autofree guint8 *chunk_buf = g_memdup2((const guint8 *)file_data + done, chunk);

		if (!fu_lenovo_accessory_command_dfu_file(FU_HIDRAW_DEVICE(self),
							  file_type,
							  done,
							  chunk_buf,
							  chunk,
							  error))
			return FALSE;

		done += chunk;
		fu_progress_set_percentage_full(fu_progress_get_child(progress), done, file_size);
	}

	return TRUE;
}

static gboolean
fu_lenovo_hid_bootloader_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	fu_lenovo_accessory_command_dfu_exit(FU_HIDRAW_DEVICE(device), 0, error);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_lenovo_hid_bootloader_setup(FuDevice *device, GError **error)
{
	g_autoptr(FuHidDescriptor) udev_des = NULL;
	g_autoptr(FuHidReport) report = NULL;
	g_autoptr(FuDevice) usb_parent = NULL;
	guint16 usb_pid = 0;
	g_autofree gchar *version = NULL;
	guint8 major = 0;
	guint8 minor = 0;
	guint8 internal = 0;
	FuHidrawDevice *udev_device = FU_HIDRAW_DEVICE(device);
	udev_des = fu_hidraw_device_parse_descriptor(udev_device, error);
	if (udev_des == NULL)
		return FALSE;
	report = fu_hid_descriptor_find_report(udev_des,
					       error,
					       "usage-page",
					       0xFF00,
					       "usage",
					       0x02,
					       "report-size",
					       8,
					       "report-count",
					       0x40,
					       NULL);
	if (report == NULL)
		return FALSE;
	if (!fu_lenovo_accessory_command_dfu_attribute(FU_HIDRAW_DEVICE(device),
						       NULL,
						       NULL,
						       &usb_pid,
						       NULL,
						       NULL,
						       NULL,
						       error))
		return FALSE;
	if (usb_pid == 0x629d)
		fu_device_add_instance_id(device, "HIDRAW\\VEN_17EF&DEV_629D");
	else if (usb_pid == 0x6201)
		fu_device_add_instance_id(device, "HIDRAW\\VEN_17EF&DEV_6201");
	else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "PID 0x%04X is not a supported Lenovo HID receiver",
			    usb_pid);
		return FALSE;
	}

	if (!fu_lenovo_accessory_command_fwversion(FU_HIDRAW_DEVICE(device),
						   &major,
						   &minor,
						   &internal,
						   error))
		return FALSE;
	version = g_strdup_printf("%u.%u.%u", major, minor, internal);
	fu_device_set_version_bootloader(device, version);
	fu_device_set_version(device, "0.0.0");
	fu_device_add_protocol(device, "com.lenovo.accessory.input.hid");
	return TRUE;
}

static void
fu_lenovo_hid_bootloader_replace(FuDevice *device, FuDevice *donor)
{
}

static gboolean
fu_lenovo_hid_bootloader_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	gsize fw_size = 0;
	gconstpointer file_raw = NULL;
	guint32 file_crc = 0;
	GBytes *file_data = fu_firmware_get_bytes(firmware, error);
	if (!file_data)
		return FALSE;
	file_raw = g_bytes_get_data(file_data, &fw_size);
	file_crc = fu_lenovo_hid_bootloader_crc32_compute(file_raw, fw_size, NULL);
	/* 进度分两阶段：prepare 5 %，写数据 95 % */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, "write");
	if (!fu_lenovo_accessory_command_dfu_prepare(FU_HIDRAW_DEVICE(device),
						     1,
						     0,
						     (guint32)fw_size,
						     file_crc,
						     error))
		return FALSE;
	fu_progress_step_done(progress); /* prepare 完成 */
	if (!fu_lenovo_hid_bootloader_write_file_data(FU_LENOVO_HID_BOOTLOADER(device),
						      1,
						      file_raw,
						      fw_size,
						      32,
						      progress,
						      error))
		return FALSE;
	fu_progress_step_done(progress);
	return TRUE;
}

static void
fu_lenovo_hid_bootloader_to_string(FuDevice *device, guint idt, GString *str)
{
}

static void
fu_lenovo_hid_bootloader_init(FuLenovoHidBootloader *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_USB_RECEIVER);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_DELL_BIOS);
	fu_device_set_name(FU_DEVICE(self), "Liyuchao lenovohid bootloader");
	fu_device_set_summary(FU_DEVICE(self), "Miniaturised USB wireless receiver (bootloader)");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	/*fu_device_register_private_flag(FU_DEVICE(self),
	FU_LENOVO_HID_BOOTLOADER_FLAG_IS_SIGNED);*/
}

static void
fu_lenovo_hid_bootloader_class_init(FuLenovoHidBootloaderClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_lenovo_hid_bootloader_to_string;
	device_class->attach = fu_lenovo_hid_bootloader_attach;
	device_class->setup = fu_lenovo_hid_bootloader_setup;
	device_class->replace = fu_lenovo_hid_bootloader_replace;
	device_class->write_firmware = fu_lenovo_hid_bootloader_write_firmware;
}
