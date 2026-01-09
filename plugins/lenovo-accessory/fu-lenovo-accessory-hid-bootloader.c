/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-hid-bootloader.h"
#include "fu-lenovo-accessory-hid-common.h"

struct _FuLenovoAccessoryHidBootloader {
	FuHidrawDevice parent_instance;
};

static void
fu_lenovo_accessory_hid_bootloader_impl_iface_init(FuLenovoAccessoryImplInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuLenovoAccessoryHidBootloader,
			fu_lenovo_accessory_hid_bootloader,
			FU_TYPE_HIDRAW_DEVICE,
			G_IMPLEMENT_INTERFACE(FU_TYPE_LENOVO_ACCESSORY_IMPL,
					      fu_lenovo_accessory_hid_bootloader_impl_iface_init))

static gboolean
fu_lenovo_accessory_hid_bootloader_write_files(FuLenovoAccessoryHidBootloader *self,
					       guint8 file_type,
					       GInputStream *stream,
					       FuProgress *progress,
					       GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_stream(stream, 0, 0, 32, error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint32 i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_lenovo_accessory_impl_dfu_file(FU_LENOVO_ACCESSORY_IMPL(self),
						       file_type,
						       fu_chunk_get_address(chk),
						       fu_chunk_get_data(chk),
						       (guint8)fu_chunk_get_data_sz(chk),
						       error))
			return FALSE;
		fu_progress_step_done(progress);
	}
	return TRUE;
}

static gboolean
fu_lenovo_accessory_hid_bootloader_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (!fu_lenovo_accessory_impl_dfu_exit(FU_LENOVO_ACCESSORY_IMPL(device), 0, error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_lenovo_accessory_hid_bootloader_setup(FuDevice *device, GError **error)
{
	guint16 usb_pid = 0;
	guint8 major = 0;
	guint8 minor = 0;
	guint8 micro = 0;
	g_autoptr(FuHidDescriptor) desc = NULL;
	g_autoptr(FuHidReport) report = NULL;
	g_autoptr(FuDevice) usb_parent = NULL;
	g_autofree gchar *version = NULL;

	desc = fu_hidraw_device_parse_descriptor(FU_HIDRAW_DEVICE(device), error);
	if (desc == NULL)
		return FALSE;
	report = fu_hid_descriptor_find_report(desc,
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

	/* add runtime counterpart */
	if (!fu_lenovo_accessory_impl_dfu_attribute(FU_LENOVO_ACCESSORY_IMPL(device),
						    NULL,
						    NULL,
						    &usb_pid,
						    NULL,
						    NULL,
						    NULL,
						    error))
		return FALSE;
	fu_device_add_instance_u16(device, "DEV", usb_pid);
	if (!fu_device_build_instance_id_full(device,
					      FU_DEVICE_INSTANCE_FLAG_COUNTERPART,
					      error,
					      "HIDRAW",
					      "VEN",
					      "DEV",
					      NULL))
		return FALSE;

	/* ensure always recoverable */
	if (!fu_lenovo_accessory_impl_get_fwversion(FU_LENOVO_ACCESSORY_IMPL(device),
						    &major,
						    &minor,
						    &micro,
						    error))
		return FALSE;
	version = g_strdup_printf("%u.%u.%u", major, minor, micro);
	fu_device_set_version_bootloader(device, version);
	fu_device_set_version(device, "0.0.0");

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_accessory_hid_bootloader_write_firmware(FuDevice *device,
						  FuFirmware *firmware,
						  FuProgress *progress,
						  FwupdInstallFlags flags,
						  GError **error)
{
	gsize fw_size = 0;
	guint32 file_crc = 0;
	g_autoptr(GInputStream) stream = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, "write");

	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_input_stream_size(stream, &fw_size, error))
		return FALSE;
	if (!fu_input_stream_compute_crc32(stream, FU_CRC_KIND_B32_STANDARD, &file_crc, error))
		return FALSE;
	if (!fu_lenovo_accessory_impl_dfu_prepare(FU_LENOVO_ACCESSORY_IMPL(device),
						  1,
						  0,
						  (guint32)fw_size,
						  file_crc,
						  error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_lenovo_accessory_hid_bootloader_write_files(
		FU_LENOVO_ACCESSORY_HID_BOOTLOADER(device),
		1,
		stream,
		fu_progress_get_child(progress),
		error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_lenovo_accessory_hid_bootloader_init(FuLenovoAccessoryHidBootloader *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.accessory");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_USB_RECEIVER);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_install_duration(FU_DEVICE(self), 18);
	fu_device_set_firmware_size_min(FU_DEVICE(self), 0x4000);
	fu_device_set_name(FU_DEVICE(self), "HID Bootloader");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_lenovo_accessory_hid_bootloader_impl_iface_init(FuLenovoAccessoryImplInterface *iface)
{
	iface->read = fu_lenovo_accessory_hid_read;
	iface->write = fu_lenovo_accessory_hid_write;
	iface->process = fu_lenovo_accessory_hid_process;
}

static void
fu_lenovo_accessory_hid_bootloader_class_init(FuLenovoAccessoryHidBootloaderClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->attach = fu_lenovo_accessory_hid_bootloader_attach;
	device_class->setup = fu_lenovo_accessory_hid_bootloader_setup;
	device_class->write_firmware = fu_lenovo_accessory_hid_bootloader_write_firmware;
}
