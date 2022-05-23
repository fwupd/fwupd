/*
 * Copyright (C) 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-steelseries-gamepad.h"

#define STEELSERIES_BUFFER_TRANSFER_SIZE 32

struct _FuSteelseriesGamepad {
	FuSteelseriesDevice parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesGamepad, fu_steelseries_gamepad, FU_TYPE_STEELSERIES_DEVICE)

static gboolean
fu_steelseries_gamepad_cmd_erase(FuDevice *device, GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0xA1, 0xAA, 0x55};

	/* USB receiver for gamepad is using different options */
	if (fu_device_has_private_flag(device, FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER)) {
		/* USB receiver */
		data[8] = 0xD0;
		data[9] = 0x01;
	} else {
		/* gamepad */
		data[9] = 0x02;
		/* magic is needed for newer gamepad */
		data[13] = 0x02;
	}

	if (!fu_steelseries_device_cmd(FU_STEELSERIES_DEVICE(device), data, FALSE, error)) {
		g_prefix_error(error, "unable erase flash block: ");
		return FALSE;
	}

	/* timeout to give some time to erase */
	g_usleep(20000);

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_setup(FuDevice *device, GError **error)
{
	g_autofree gchar *bootloader_version = NULL;
	g_autofree gchar *version = NULL;
	guint16 fw_ver;
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0};

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* get version of FW and bootloader */
	data[0] = 0x12;
	if (!fu_steelseries_device_cmd(FU_STEELSERIES_DEVICE(device), data, TRUE, error))
		return FALSE;

	if (!fu_common_read_uint16_safe(data, sizeof(data), 0x01, &fw_ver, G_LITTLE_ENDIAN, error))
		return FALSE;
	version = fu_common_version_from_uint16(fw_ver, FWUPD_VERSION_FORMAT_BCD);
	fu_device_set_version(FU_DEVICE(device), version);

	if (!fu_common_read_uint16_safe(data, sizeof(data), 0x03, &fw_ver, G_LITTLE_ENDIAN, error))
		return FALSE;
	bootloader_version = fu_common_version_from_uint16(fw_ver, FWUPD_VERSION_FORMAT_BCD);
	fu_device_set_version_bootloader(device, bootloader_version);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_gamepad_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0xA6, 0xAA, 0x55};
	g_autoptr(GError) error_local = NULL;

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* switch to runtime mode */
	if (!fu_steelseries_device_cmd(FU_STEELSERIES_DEVICE(device), data, FALSE, &error_local))
		g_debug("ignoring error on reset: %s", error_local->message);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0x02, 0x08};
	g_autoptr(GError) error_local = NULL;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* switch to bootloader mode */
	if (!fu_steelseries_device_cmd(FU_STEELSERIES_DEVICE(device), data, FALSE, &error_local))
		g_debug("ignoring error on reset: %s", error_local->message);

	/* controller will be renumbered after switching to bootloader mode */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_write_firmware_chunks(FuDevice *device,
					     GPtrArray *chunks,
					     FuProgress *progress,
					     guint32 *checksum,
					     GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);

	for (guint id = 0; id < chunks->len; id++) {
		FuChunk *chunk = g_ptr_array_index(chunks, id);
		guint16 chunk_checksum;
		guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0xA3};

		/* block ID */
		if (!fu_common_write_uint16_safe(data,
						 STEELSERIES_BUFFER_CONTROL_SIZE,
						 0x01,
						 (guint16)id,
						 G_LITTLE_ENDIAN,
						 error))
			return FALSE;
		/* 32B of data only */
		if (!fu_memcpy_safe(data,
				    STEELSERIES_BUFFER_CONTROL_SIZE,
				    0x03,
				    fu_chunk_get_data(chunk),
				    STEELSERIES_BUFFER_TRANSFER_SIZE,
				    0,
				    fu_chunk_get_data_sz(chunk),
				    error))
			return FALSE;

		/* block checksum */
		/* probably not necessary */
		chunk_checksum = fu_common_sum16(data + 3, STEELSERIES_BUFFER_TRANSFER_SIZE);
		if (!fu_common_write_uint16_safe(data,
						 STEELSERIES_BUFFER_CONTROL_SIZE,
						 0x03 + STEELSERIES_BUFFER_TRANSFER_SIZE,
						 chunk_checksum,
						 G_LITTLE_ENDIAN,
						 error))
			return FALSE;

		*checksum += (guint32)chunk_checksum;

		if (!fu_steelseries_device_cmd(FU_STEELSERIES_DEVICE(device), data, FALSE, error)) {
			g_prefix_error(error, "unable to flash block %u: ", id);
			return FALSE;
		}
		/* timeout to give some time to flash the block on device */
		g_usleep(10000);
		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_write_checksum(FuDevice *device, guint32 checksum, GError **error)
{
	/* write checksum */
	guint8 data[STEELSERIES_BUFFER_CONTROL_SIZE] = {0xA5, 0xAA, 0x55};

	if (!fu_common_write_uint32_safe(data,
					 STEELSERIES_BUFFER_CONTROL_SIZE,
					 0x03,
					 checksum,
					 G_LITTLE_ENDIAN,
					 error))
		return FALSE;

	if (!fu_steelseries_device_cmd(FU_STEELSERIES_DEVICE(device), data, TRUE, error)) {
		g_prefix_error(error, "unable to write checksum: ");
		return FALSE;
	}

	/* validate checksum */
	if (data[0] != 0xA5 || data[1] != 0xAA || data[2] != 0x55 || data[3] != 0x01) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "Controller is unable to validate checksum");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	guint32 checksum = 0;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;

	chunks = fu_chunk_array_new_from_bytes(blob, 0, 0, STEELSERIES_BUFFER_TRANSFER_SIZE);

	if (chunks->len > (G_MAXUINT16 + 1)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "too lot of firmware chunks for the device");
		return FALSE;
	}

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 0);

	/* erase all first */
	if (!fu_steelseries_gamepad_cmd_erase(device, error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_steelseries_gamepad_write_firmware_chunks(device,
							  chunks,
							  fu_progress_get_child(progress),
							  &checksum,
							  error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_steelseries_gamepad_write_checksum(device, checksum, error))
		return FALSE;
	fu_progress_step_done(progress);

	return TRUE;
}

static void
fu_steelseries_gamepad_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 93);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5);	/* reload */
}

static void
fu_steelseries_gamepad_class_init(FuSteelseriesGamepadClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	klass_device->setup = fu_steelseries_gamepad_setup;
	klass_device->attach = fu_steelseries_gamepad_attach;
	klass_device->detach = fu_steelseries_gamepad_detach;
	klass_device->write_firmware = fu_steelseries_gamepad_write_firmware;
	klass_device->set_progress = fu_steelseries_gamepad_set_progress;
}

static void
fu_steelseries_gamepad_init(FuSteelseriesGamepad *self)
{
	fu_steelseries_device_set_iface_idx_offset(FU_STEELSERIES_DEVICE(self), -1);

	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_BCD);

	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_protocol(FU_DEVICE(self), "com.steelseries.gamepad");

	fu_device_set_firmware_size_max(FU_DEVICE(self),
					(G_MAXUINT16 + 1) * STEELSERIES_BUFFER_TRANSFER_SIZE);
}
