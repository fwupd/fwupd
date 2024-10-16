/*
 * Copyright 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-steelseries-gamepad-struct.h"
#include "fu-steelseries-gamepad.h"

struct _FuSteelseriesGamepad {
	FuSteelseriesDevice parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesGamepad, fu_steelseries_gamepad, FU_TYPE_STEELSERIES_DEVICE)

static gboolean
fu_steelseries_gamepad_cmd_erase(FuSteelseriesGamepad *self, GError **error)
{
	g_autoptr(FuStructSteelseriesGamepadEraseReq) st_req =
	    fu_struct_steelseries_gamepad_erase_req_new();

	/* USB receiver for gamepad is using different options */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_STEELSERIES_DEVICE_FLAG_IS_RECEIVER)) {
		/* USB receiver */
		fu_struct_steelseries_gamepad_erase_req_set_unknown08(st_req, 0xD0);
		fu_struct_steelseries_gamepad_erase_req_set_unknown09(st_req, 0x01);
	} else {
		/* gamepad */
		fu_struct_steelseries_gamepad_erase_req_set_unknown09(st_req, 0x02);
		/* magic is needed for newer gamepad */
		fu_struct_steelseries_gamepad_erase_req_set_unknown13(st_req, 0x02);
	}
	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error)) {
		g_prefix_error(error, "unable erase flash block: ");
		return FALSE;
	}

	/* timeout to give some time to erase */
	fu_device_sleep(FU_DEVICE(self), 20); /* ms */

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_setup(FuDevice *device, GError **error)
{
	FuSteelseriesGamepad *self = FU_STEELSERIES_GAMEPAD(device);
	g_autofree gchar *bootloader_version = NULL;
	g_autoptr(FuStructSteelseriesGamepadGetVersionsReq) st_req =
	    fu_struct_steelseries_gamepad_get_versions_req_new();
	g_autoptr(FuStructSteelseriesGamepadGetVersionsRes) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* get version of FW and bootloader */
	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error))
		return FALSE;
	buf_res = fu_steelseries_device_response(FU_STEELSERIES_DEVICE(self), error);
	if (buf_res == NULL)
		return FALSE;
	st_res = fu_struct_steelseries_gamepad_get_versions_res_parse(buf_res->data,
								      buf_res->len,
								      0x0,
								      error);
	if (st_res == NULL)
		return FALSE;
	fu_device_set_version_raw(
	    FU_DEVICE(device),
	    fu_struct_steelseries_gamepad_get_versions_res_get_runtime_version(st_res));
	bootloader_version = fu_version_from_uint16(
	    fu_struct_steelseries_gamepad_get_versions_res_get_bootloader_version(st_res),
	    FWUPD_VERSION_FORMAT_BCD);
	fu_device_set_version_bootloader(device, bootloader_version);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_gamepad_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSteelseriesGamepad *self = FU_STEELSERIES_GAMEPAD(device);
	g_autoptr(FuStructSteelseriesGamepadBootRuntimeReq) st_req =
	    fu_struct_steelseries_gamepad_boot_runtime_req_new();
	g_autoptr(GError) error_local = NULL;

	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* switch to runtime mode */
	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, &error_local))
		g_debug("ignoring error on reset: %s", error_local->message);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSteelseriesGamepad *self = FU_STEELSERIES_GAMEPAD(device);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuStructSteelseriesGamepadBootLoaderReq) st_req =
	    fu_struct_steelseries_gamepad_boot_loader_req_new();

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	/* switch to bootloader mode */
	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, &error_local))
		g_debug("ignoring error on reset: %s", error_local->message);

	/* controller will be renumbered after switching to bootloader mode */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_write_firmware_chunk(FuSteelseriesGamepad *self,
					    FuChunk *chunk,
					    guint32 *checksum,
					    GError **error)
{
	guint16 chunk_checksum;
	g_autoptr(FuStructSteelseriesGamepadWriteChunkReq) st_req =
	    fu_struct_steelseries_gamepad_write_chunk_req_new();
	g_autoptr(GBytes) blob = NULL;

	blob = fu_chunk_get_bytes(chunk, error);
	if (blob == NULL)
		return FALSE;

	/* block ID, 32B of data then block checksum -- probably not necessary */
	fu_struct_steelseries_gamepad_write_chunk_req_set_block_id(st_req, fu_chunk_get_idx(chunk));
	if (!fu_struct_steelseries_gamepad_write_chunk_req_set_data(st_req,
								    g_bytes_get_data(blob, NULL),
								    g_bytes_get_size(blob),
								    error))
		return FALSE;
	chunk_checksum = fu_sum16_bytes(blob);
	fu_struct_steelseries_gamepad_write_chunk_req_set_checksum(st_req, chunk_checksum);
	*checksum += (guint32)chunk_checksum;

	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error)) {
		g_prefix_error(error, "unable to flash block %u: ", fu_chunk_get_idx(chunk));
		return FALSE;
	}

	/* timeout to give some time to flash the block on device */
	fu_device_sleep(FU_DEVICE(self), 10); /* ms */
	return TRUE;
}

static gboolean
fu_steelseries_gamepad_write_firmware_chunks(FuSteelseriesGamepad *self,
					     FuChunkArray *chunks,
					     FuProgress *progress,
					     guint32 *checksum,
					     GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chunk = NULL;

		chunk = fu_chunk_array_index(chunks, i, error);
		if (chunk == NULL)
			return FALSE;
		if (!fu_steelseries_gamepad_write_firmware_chunk(self, chunk, checksum, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_steelseries_gamepad_write_checksum(FuSteelseriesGamepad *self, guint32 checksum, GError **error)
{
	g_autoptr(FuStructSteelseriesGamepadWriteChecksumReq) st_req =
	    fu_struct_steelseries_gamepad_write_checksum_req_new();
	g_autoptr(FuStructSteelseriesGamepadWriteChecksumRes) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	fu_struct_steelseries_gamepad_write_checksum_req_set_checksum(st_req, checksum);
	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error)) {
		g_prefix_error(error, "unable to write checksum: ");
		return FALSE;
	}
	buf_res = fu_steelseries_device_response(FU_STEELSERIES_DEVICE(self), error);
	if (buf_res == NULL)
		return FALSE;

	/* validate checksum */
	st_res = fu_struct_steelseries_gamepad_write_checksum_res_parse(buf_res->data,
									buf_res->len,
									0x0,
									error);
	if (st_res == NULL) {
		g_prefix_error(error, "controller is unable to validate checksum: ");
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
	FuSteelseriesGamepad *self = FU_STEELSERIES_GAMEPAD(device);
	guint32 checksum = 0;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_bytes(blob,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       FU_STRUCT_STEELSERIES_GAMEPAD_WRITE_CHUNK_REQ_SIZE_DATA);
	if (fu_chunk_array_length(chunks) > (G_MAXUINT16 + 1)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "too many firmware chunks for the device");
		return FALSE;
	}

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1, NULL);

	/* erase all first */
	if (!fu_steelseries_gamepad_cmd_erase(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_steelseries_gamepad_write_firmware_chunks(self,
							  chunks,
							  fu_progress_get_child(progress),
							  &checksum,
							  error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_steelseries_gamepad_write_checksum(self, checksum, error))
		return FALSE;
	fu_progress_step_done(progress);

	return TRUE;
}

static void
fu_steelseries_gamepad_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 93, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "reload");
}

static gchar *
fu_steelseries_gamepad_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16(version_raw, fu_device_get_version_format(device));
}

static void
fu_steelseries_gamepad_class_init(FuSteelseriesGamepadClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_steelseries_gamepad_setup;
	device_class->attach = fu_steelseries_gamepad_attach;
	device_class->detach = fu_steelseries_gamepad_detach;
	device_class->write_firmware = fu_steelseries_gamepad_write_firmware;
	device_class->set_progress = fu_steelseries_gamepad_set_progress;
	device_class->convert_version = fu_steelseries_gamepad_convert_version;
}

static void
fu_steelseries_gamepad_init(FuSteelseriesGamepad *self)
{
	fu_steelseries_device_set_iface_idx_offset(FU_STEELSERIES_DEVICE(self), -1);

	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_BCD);

	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_protocol(FU_DEVICE(self), "com.steelseries.gamepad");

	fu_device_set_firmware_size_max(
	    FU_DEVICE(self),
	    (G_MAXUINT16 + 1) * FU_STRUCT_STEELSERIES_GAMEPAD_WRITE_CHUNK_REQ_SIZE_DATA);
}
