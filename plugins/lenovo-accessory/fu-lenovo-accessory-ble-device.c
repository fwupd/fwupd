/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-lenovo-accessory-ble-common.h"
#include "fu-lenovo-accessory-ble-device.h"

struct _FuLenovoAccessoryBleDevice {
	FuBluezDevice parent_instance;
};

G_DEFINE_TYPE(FuLenovoAccessoryBleDevice, fu_lenovo_accessory_ble_device, FU_TYPE_BLUEZ_DEVICE)

#define UUID_WRITE "c1d02501-2d1f-400a-95d2-6a2f7bca0c25"
#define UUID_READ  "c1d02502-2d1f-400a-95d2-6a2f7bca0c25"

static gboolean
fu_lenovo_accessory_ble_device_write_files(FuLenovoAccessoryBleDevice *self,
					   guint8 file_type,
					   GBytes *blob,
					   FuProgress *progress,
					   GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_bytes(blob, 0, 0, 32);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint32 i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_lenovo_accessory_ble_dfu_file(FU_BLUEZ_DEVICE(self),
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
fu_lenovo_accessory_ble_device_write_firmware(FuDevice *device,
					      FuFirmware *firmware,
					      FuProgress *progress,
					      FwupdInstallFlags flags,
					      GError **error)
{
	gsize fw_size = 0;
	guint32 file_crc = 0;
	guint32 device_crc = 0;
	g_autoptr(GBytes) blob = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "prepare");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, "write");

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	fw_size = g_bytes_get_size(blob);
	file_crc = fu_crc32_bytes(FU_CRC_KIND_B32_STANDARD, blob);

	if (!fu_lenovo_accessory_ble_dfu_entry(FU_BLUEZ_DEVICE(device), error))
		return FALSE;
	if (!fu_lenovo_accessory_ble_dfu_prepare(FU_BLUEZ_DEVICE(device),
						 1,
						 0,
						 (guint32)fw_size,
						 file_crc,
						 error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_lenovo_accessory_ble_device_write_files(FU_LENOVO_ACCESSORY_BLE_DEVICE(device),
							1,
							blob,
							fu_progress_get_child(progress),
							error))
		return FALSE;
	if (!fu_lenovo_accessory_ble_dfu_crc(FU_BLUEZ_DEVICE(device), &device_crc, error)) {
		g_prefix_error(error, "BLE CRC Error (device 0x%08x): ", device_crc);
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
fu_lenovo_accessory_ble_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (!fu_lenovo_accessory_ble_dfu_exit(FU_BLUEZ_DEVICE(device), 0, error)) {
		g_prefix_error_literal(error, "failed to exit: ");
		return FALSE;
	}
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_lenovo_accessory_ble_device_setup(FuDevice *device, GError **error)
{
	guint8 major = 0;
	guint8 minor = 0;
	guint8 micro = 0;
	g_autofree gchar *version = NULL;

	if (!fu_lenovo_accessory_ble_fwversion(FU_BLUEZ_DEVICE(device),
					       &major,
					       &minor,
					       &micro,
					       error))
		return FALSE;
	version = g_strdup_printf("%u.%u.%02u", major, minor, micro);
	fu_device_set_version(device, version);
	return TRUE;
}

static void
fu_lenovo_accessory_ble_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_lenovo_accessory_ble_device_probe(FuDevice *device, GError **error)
{
	if (!FU_DEVICE_CLASS(fu_lenovo_accessory_ble_device_parent_class)->probe(device, error))
		return FALSE;

	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	return TRUE;
}

static void
fu_lenovo_accessory_ble_device_init(FuLenovoAccessoryBleDevice *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), 10000); /* ms */
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.accessory");
	fu_device_set_install_duration(FU_DEVICE(self), 60);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
}

static void
fu_lenovo_accessory_ble_device_class_init(FuLenovoAccessoryBleDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_lenovo_accessory_ble_device_write_firmware;
	device_class->set_progress = fu_lenovo_accessory_ble_device_set_progress;
	device_class->setup = fu_lenovo_accessory_ble_device_setup;
	device_class->probe = fu_lenovo_accessory_ble_device_probe;
	device_class->attach = fu_lenovo_accessory_ble_device_attach;
}
