/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-hid-common.h"
#include "fu-lenovo-accessory-hid-dual-device.h"

struct _FuLenovoAccessoryHidDualDevice {
	FuHidDevice parent_instance;
};

static void
fu_lenovo_accessory_hid_dual_device_impl_iface_init(FuLenovoAccessoryImplInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuLenovoAccessoryHidDualDevice,
			fu_lenovo_accessory_hid_dual_device,
			FU_TYPE_HID_DEVICE,
			G_IMPLEMENT_INTERFACE(FU_TYPE_LENOVO_ACCESSORY_IMPL,
					      fu_lenovo_accessory_hid_dual_device_impl_iface_init))

static gboolean
fu_lenovo_accessory_hid_dual_device_write_files(FuLenovoAccessoryHidDualDevice *self,
						FuLenovoAccessoryDfuFileType file_type,
						FuInputStream *stream,
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
						       fu_chunk_get_data_sz(chk),
						       error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_accessory_hid_dual_device_write_firmware(FuDevice *device,
						   FuFirmware *firmware,
						   FuProgress *progress,
						   FwupdInstallFlags flags,
						   GError **error)
{
	FuLenovoAccessoryHidDualDevice *self = FU_LENOVO_ACCESSORY_HID_DUAL_DEVICE(device);
	gsize fw_size = 0;
	guint32 file_crc = 0xFFFFFFFF;
	guint32 device_crc = 0;
	FuLenovoAccessoryDeviceMode mode;
	g_autoptr(FuInputStream) stream = NULL;

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

	/* only enter DFU mode if not already there */
	if (!fu_lenovo_accessory_impl_get_mode(FU_LENOVO_ACCESSORY_IMPL(device), &mode, error))
		return FALSE;
	if (mode != FU_LENOVO_ACCESSORY_DEVICE_MODE_DFU_MODE) {
		if (!fu_lenovo_accessory_impl_dfu_entry(FU_LENOVO_ACCESSORY_IMPL(device), error))
			return FALSE;
	}
	if (!fu_lenovo_accessory_impl_dfu_attribute(FU_LENOVO_ACCESSORY_IMPL(device),
						    NULL,
						    NULL,
						    NULL,
						    NULL,
						    NULL,
						    NULL,
						    error))
		return FALSE;
	if (!fu_lenovo_accessory_impl_dfu_prepare(FU_LENOVO_ACCESSORY_IMPL(device),
						  FU_LENOVO_ACCESSORY_DFU_FILE_TYPE_BIN_FILE,
						  0x0,
						  (guint32)fw_size,
						  file_crc,
						  error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_lenovo_accessory_hid_dual_device_write_files(
		self,
		FU_LENOVO_ACCESSORY_DFU_FILE_TYPE_BIN_FILE,
		stream,
		fu_progress_get_child(progress),
		error))
		return FALSE;

	/* give the device time to finalize the flash before reading back CRC */
	fu_device_sleep(FU_DEVICE(self), 2000);
	if (!fu_lenovo_accessory_impl_dfu_crc(FU_LENOVO_ACCESSORY_IMPL(device),
					      &device_crc,
					      error)) {
		g_prefix_error_literal(error, "failed to read device CRC: ");
		return FALSE;
	}
	if (device_crc != file_crc) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "CRC mismatch: device 0x%08x != file 0x%08x",
			    device_crc,
			    file_crc);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_accessory_hid_dual_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (!fu_lenovo_accessory_impl_dfu_exit(FU_LENOVO_ACCESSORY_IMPL(device),
					       FU_LENOVO_ACCESSORY_DFU_EXIT_CODE_DFU_SUCCESS,
					       error)) {
		g_prefix_error_literal(error, "failed to exit: ");
		return FALSE;
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_lenovo_accessory_hid_dual_device_setup(FuDevice *device, GError **error)
{
	FuLenovoAccessoryHidDualDevice *self = FU_LENOVO_ACCESSORY_HID_DUAL_DEVICE(device);
	guint8 major = 0;
	guint8 minor = 0;
	guint8 micro = 0;
	g_autofree gchar *version = NULL;
	g_autoptr(GPtrArray) descriptors = NULL;
	gboolean found_report = FALSE;

	/* the command interface is a vendor HID collection (UsagePage 0xFF00,
	 * Usage 0x02) carrying a 64-byte report; confirm it is present so we
	 * fail early on a device that does not speak this protocol */
	descriptors = fu_hid_device_parse_descriptors(FU_HID_DEVICE(self), error);
	if (descriptors == NULL)
		return FALSE;
	for (guint i = 0; i < descriptors->len; i++) {
		FuHidDescriptor *desc = g_ptr_array_index(descriptors, i);
		g_autoptr(FuHidReport) report = NULL;
		report = fu_hid_descriptor_find_report(desc,
						       NULL,
						       "usage-page",
						       0xFF00,
						       "usage",
						       0x02,
						       "report-size",
						       8,
						       "report-count",
						       0x40,
						       NULL);
		if (report != NULL) {
			found_report = TRUE;
			break;
		}
	}
	if (!found_report) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no vendor command report (0xFF00/0x02) found");
		return FALSE;
	}
	if (!fu_lenovo_accessory_impl_get_fwversion(FU_LENOVO_ACCESSORY_IMPL(device),
						    &major,
						    &minor,
						    &micro,
						    error))
		return FALSE;
	version = g_strdup_printf("%u.%u.%u", major, minor, micro);
	fu_device_set_version(device, version);
	return fu_lenovo_accessory_hid_add_children(FU_LENOVO_ACCESSORY_IMPL(device), error);
}

static void
fu_lenovo_accessory_hid_dual_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_lenovo_accessory_hid_dual_device_impl_iface_init(FuLenovoAccessoryImplInterface *iface)
{
	iface->read = fu_lenovo_accessory_hid_read;
	iface->write = fu_lenovo_accessory_hid_write;
	iface->process = fu_lenovo_accessory_hid_process;
}

static void
fu_lenovo_accessory_hid_dual_device_class_init(FuLenovoAccessoryHidDualDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_lenovo_accessory_hid_dual_device_write_firmware;
	device_class->set_progress = fu_lenovo_accessory_hid_dual_device_set_progress;
	device_class->setup = fu_lenovo_accessory_hid_dual_device_setup;
	device_class->attach = fu_lenovo_accessory_hid_dual_device_attach;
}

static void
fu_lenovo_accessory_hid_dual_device_init(FuLenovoAccessoryHidDualDevice *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), 10000); /* ms */
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.accessory");
	fu_device_set_install_duration(FU_DEVICE(self), 60);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_hid_device_set_interface(FU_HID_DEVICE(self), FU_LENOVO_ACCESSORY_IFACE_CMD);
}
