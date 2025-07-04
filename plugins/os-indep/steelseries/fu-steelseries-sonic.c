/*
 * Copyright 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-steelseries-sonic-struct.h"
#include "fu-steelseries-sonic.h"

#define FU_STEELSERIES_BUFFER_FLASH_TRANSFER_SIZE 128

const guint16 FU_STEELSERIES_SONIC_READ_FROM_RAM_OPCODE[] = {0x00c3U, 0x00c3U, 0x0083U};
const guint16 FU_STEELSERIES_SONIC_READ_FROM_FLASH_OPCODE[] = {0x00c5U, 0x00c5U, 0x0085U};
const guint16 FU_STEELSERIES_SONIC_WRITE_TO_RAM_OPCODE[] = {0x0043U, 0x0043U, 0x0003U};
const guint16 FU_STEELSERIES_SONIC_WRITE_TO_FLASH_OPCODE[] = {0x0045U, 0x0045U, 0x0005U};
const guint16 FU_STEELSERIES_SONIC_ERASE_OPCODE[] = {0x0048U, 0x0048U, 0x0008U};
const guint16 FU_STEELSERIES_SONIC_RESTART_OPCODE[] = {0x0041U, 0x0041U, 0x0001U};
const guint16 FU_STEELSERIES_SONIC_CHIP[] = {0x0002U, 0x0003U, 0x0002U};
const guint32 FU_STEELSERIES_SONIC_FIRMWARE_SIZE[] = {0x9000U, 0x4000U, 0x12000U};
const gchar *FU_STEELSERIES_SONIC_FIRMWARE_ID[] = {
    "app-nordic.bin",
    "app-holtek.bin",
    "mouse-app.bin",
};
const guint FU_STEELSERIES_SONIC_WRITE_PROGRESS_STEP_VALUE[][2] = {{5, 95}, {11, 89}, {3, 97}};

struct _FuSteelseriesSonic {
	FuSteelseriesDevice parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesSonic, fu_steelseries_sonic, FU_TYPE_STEELSERIES_DEVICE)

static gboolean
fu_steelseries_sonic_wireless_status(FuSteelseriesSonic *self,
				     FuSteelseriesSonicWirelessStatus *status,
				     GError **error)
{
	g_autoptr(FuStructSteelseriesSonicWirelessStatusReq) st_req =
	    fu_struct_steelseries_sonic_wireless_status_req_new();
	g_autoptr(FuStructSteelseriesSonicWirelessStatusRes) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error))
		return FALSE;
	buf_res = fu_steelseries_device_response(FU_STEELSERIES_DEVICE(self), error);
	if (buf_res == NULL)
		return FALSE;
	st_res = fu_struct_steelseries_sonic_wireless_status_res_parse(buf_res->data,
								       buf_res->len,
								       0x0,
								       error);
	if (st_res == NULL)
		return FALSE;
	*status = fu_struct_steelseries_sonic_wireless_status_res_get_status(st_res);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_ensure_battery_state(FuSteelseriesSonic *self, GError **error)
{
	g_autoptr(FuStructSteelseriesSonicBatteryReq) st_req =
	    fu_struct_steelseries_sonic_battery_req_new();
	g_autoptr(FuStructSteelseriesSonicBatteryRes) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error))
		return FALSE;
	buf_res = fu_steelseries_device_response(FU_STEELSERIES_DEVICE(self), error);
	if (buf_res == NULL)
		return FALSE;
	st_res =
	    fu_struct_steelseries_sonic_battery_res_parse(buf_res->data, buf_res->len, 0x0, error);
	if (st_res == NULL)
		return FALSE;
	fu_device_set_battery_level(FU_DEVICE(self),
				    fu_struct_steelseries_sonic_battery_res_get_value(st_res));

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_read_from_ram_chunk(FuSteelseriesSonic *self,
					 FuSteelseriesSonicChip chip,
					 FuChunk *chk,
					 GError **error)
{
	const guint8 *data;
	gsize datasz = 0;
	g_autoptr(FuStructSteelseriesSonicReadFromRamReq) st_req =
	    fu_struct_steelseries_sonic_read_from_ram_req_new();
	g_autoptr(FuStructSteelseriesSonicReadFromRamRes) st_res = NULL;
	g_autoptr(GByteArray) buf_res = NULL;

	fu_struct_steelseries_sonic_read_from_ram_req_set_opcode(
	    st_req,
	    FU_STEELSERIES_SONIC_READ_FROM_RAM_OPCODE[chip]);
	fu_struct_steelseries_sonic_read_from_ram_req_set_offset(st_req, fu_chunk_get_address(chk));
	fu_struct_steelseries_sonic_read_from_ram_req_set_size(st_req, fu_chunk_get_data_sz(chk));
	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error))
		return FALSE;

	buf_res = fu_steelseries_device_response(FU_STEELSERIES_DEVICE(self), error);
	st_res = fu_struct_steelseries_sonic_read_from_ram_res_parse(buf_res->data,
								     buf_res->len,
								     0x0,
								     error);
	if (st_res == NULL)
		return FALSE;
	data = fu_struct_steelseries_sonic_read_from_ram_res_get_data(st_res, &datasz);
	return fu_memcpy_safe(fu_chunk_get_data_out(chk),
			      fu_chunk_get_data_sz(chk),
			      0x0,
			      data,
			      datasz,
			      0x0,
			      fu_chunk_get_data_sz(chk),
			      error);
}

static gboolean
fu_steelseries_sonic_read_from_ram(FuSteelseriesSonic *self,
				   FuSteelseriesSonicChip chip,
				   guint32 address,
				   guint8 *buf,
				   guint16 bufsz,
				   FuProgress *progress,
				   GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	chunks =
	    fu_chunk_array_mutable_new(buf,
				       bufsz,
				       0x0,
				       0x0,
				       FU_STRUCT_STEELSERIES_SONIC_READ_FROM_RAM_RES_SIZE_DATA);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_steelseries_sonic_read_from_ram_chunk(self, chip, chk, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_read_from_flash_chunk(FuSteelseriesSonic *self,
					   FuSteelseriesSonicChip chip,
					   FuChunk *chk,
					   FuProgress *progress,
					   GError **error)
{
	g_autoptr(FuStructSteelseriesSonicReadFromFlashReq) st_req =
	    fu_struct_steelseries_sonic_read_from_flash_req_new();

	fu_struct_steelseries_sonic_read_from_flash_req_set_opcode(
	    st_req,
	    FU_STEELSERIES_SONIC_READ_FROM_FLASH_OPCODE[chip]);
	fu_struct_steelseries_sonic_read_from_flash_req_set_chipid(st_req,
								   FU_STEELSERIES_SONIC_CHIP[chip]);
	fu_struct_steelseries_sonic_read_from_flash_req_set_offset(st_req,
								   fu_chunk_get_address(chk));
	fu_struct_steelseries_sonic_read_from_flash_req_set_size(st_req, fu_chunk_get_data_sz(chk));
	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error))
		return FALSE;

	/* timeout to give some time to read from flash to ram */
	fu_device_sleep(FU_DEVICE(self), 15); /* ms */

	return fu_steelseries_sonic_read_from_ram(self,
						  chip,
						  fu_chunk_get_address(chk),
						  fu_chunk_get_data_out(chk),
						  fu_chunk_get_data_sz(chk),
						  progress,
						  error);
}

static gboolean
fu_steelseries_sonic_read_from_flash(FuSteelseriesSonic *self,
				     FuSteelseriesSonicChip chip,
				     guint32 address,
				     guint8 *buf,
				     gsize bufsz,
				     FuProgress *progress,
				     GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_mutable_new(buf,
					    bufsz,
					    address,
					    0x0,
					    FU_STEELSERIES_BUFFER_FLASH_TRANSFER_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_steelseries_sonic_read_from_flash_chunk(self,
								chip,
								chk,
								fu_progress_get_child(progress),
								error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_write_to_ram_chunk(FuSteelseriesSonic *self,
					FuSteelseriesSonicChip chip,
					FuChunk *chk,
					GError **error)
{
	g_autoptr(FuStructSteelseriesSonicWriteToRamReq) st_req =
	    fu_struct_steelseries_sonic_write_to_ram_req_new();
	fu_struct_steelseries_sonic_write_to_ram_req_set_opcode(
	    st_req,
	    FU_STEELSERIES_SONIC_WRITE_TO_RAM_OPCODE[chip]);
	fu_struct_steelseries_sonic_write_to_ram_req_set_offset(st_req, fu_chunk_get_address(chk));
	fu_struct_steelseries_sonic_write_to_ram_req_set_size(st_req, fu_chunk_get_data_sz(chk));
	if (!fu_struct_steelseries_sonic_write_to_ram_req_set_data(st_req,
								   fu_chunk_get_data(chk),
								   fu_chunk_get_data_sz(chk),
								   error))
		return FALSE;
	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error))
		return FALSE;

	/* timeout to give some time to write to ram */
	fu_device_sleep(FU_DEVICE(self), 15); /* ms */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_write_to_ram(FuSteelseriesSonic *self,
				  FuSteelseriesSonicChip chip,
				  guint16 address,
				  GBytes *fw,
				  FuProgress *progress,
				  GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks =
	    fu_chunk_array_new_from_bytes(fw,
					  FU_CHUNK_ADDR_OFFSET_NONE,
					  FU_CHUNK_PAGESZ_NONE,
					  FU_STRUCT_STEELSERIES_SONIC_WRITE_TO_RAM_REQ_SIZE_DATA);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_steelseries_sonic_write_to_ram_chunk(self, chip, chk, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_write_to_flash_chunk(FuSteelseriesSonic *self,
					  FuSteelseriesSonicChip chip,
					  FuChunk *chk,
					  FuProgress *progress,
					  GError **error)
{
	g_autoptr(GBytes) chk_blob = NULL;
	g_autoptr(FuStructSteelseriesSonicWriteToFlashReq) st_req =
	    fu_struct_steelseries_sonic_write_to_flash_req_new();

	chk_blob = fu_chunk_get_bytes(chk);
	if (!fu_steelseries_sonic_write_to_ram(self,
					       chip,
					       fu_chunk_get_address(chk),
					       chk_blob,
					       progress,
					       error))
		return FALSE;

	fu_struct_steelseries_sonic_write_to_flash_req_set_opcode(
	    st_req,
	    FU_STEELSERIES_SONIC_WRITE_TO_FLASH_OPCODE[chip]);
	fu_struct_steelseries_sonic_write_to_flash_req_set_chipid(st_req,
								  FU_STEELSERIES_SONIC_CHIP[chip]);
	fu_struct_steelseries_sonic_write_to_flash_req_set_offset(st_req,
								  fu_chunk_get_address(chk));
	fu_struct_steelseries_sonic_write_to_flash_req_set_size(st_req, fu_chunk_get_data_sz(chk));
	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error))
		return FALSE;

	/* timeout to give some time to write from ram to flash */
	fu_device_sleep(FU_DEVICE(self), 15); /* ms */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_write_to_flash(FuSteelseriesSonic *self,
				    FuSteelseriesSonicChip chip,
				    guint32 address,
				    GBytes *fw,
				    FuProgress *progress,
				    GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_bytes(fw,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       FU_STEELSERIES_BUFFER_FLASH_TRANSFER_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_steelseries_sonic_write_to_flash_chunk(self,
							       chip,
							       chk,
							       fu_progress_get_child(progress),
							       error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_erase(FuSteelseriesSonic *self,
			   FuSteelseriesSonicChip chip,
			   FuProgress *progress,
			   GError **error)
{
	g_autoptr(FuStructSteelseriesSonicEraseReq) st_req =
	    fu_struct_steelseries_sonic_erase_req_new();

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_ERASE);
	fu_progress_set_steps(progress, 1);

	fu_struct_steelseries_sonic_erase_req_set_opcode(st_req,
							 FU_STEELSERIES_SONIC_ERASE_OPCODE[chip]);
	fu_struct_steelseries_sonic_erase_req_set_chipid(st_req, FU_STEELSERIES_SONIC_CHIP[chip]);
	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error))
		return FALSE;

	/* timeout to give some time to erase flash */
	fu_device_sleep_full(FU_DEVICE(self), 1000, fu_progress_get_child(progress)); /* ms */
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_restart(FuSteelseriesSonic *self,
			     FuSteelseriesSonicChip chip,
			     FuProgress *progress,
			     GError **error)
{
	g_autoptr(FuStructSteelseriesSonicRestartReq) st_req =
	    fu_struct_steelseries_sonic_restart_req_new();

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_RESTART);
	fu_progress_set_steps(progress, 1);

	fu_struct_steelseries_sonic_restart_req_set_opcode(
	    st_req,
	    FU_STEELSERIES_SONIC_RESTART_OPCODE[chip]);
	if (!fu_steelseries_device_request(FU_STEELSERIES_DEVICE(self), st_req, error))
		return FALSE;

	/* timeout to give some time to restart chip */
	fu_device_sleep_full(FU_DEVICE(self), 3000, progress); /* ms */
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_wait_for_connect_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuSteelseriesSonic *self = FU_STEELSERIES_SONIC(device);
	FuSteelseriesSonicWirelessStatus *wl_status = (FuSteelseriesSonicWirelessStatus *)user_data;

	if (!fu_steelseries_sonic_wireless_status(self, wl_status, error)) {
		g_prefix_error(error, "failed to get wireless status: ");
		return FALSE;
	}
	g_debug("WirelessStatus: %s", fu_steelseries_sonic_wireless_status_to_string(*wl_status));
	if (*wl_status != FU_STEELSERIES_SONIC_WIRELESS_STATUS_CONNECTED) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "device is unreachable");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_wait_for_connect(FuSteelseriesSonic *self,
				      guint delay,
				      FuProgress *progress,
				      GError **error)
{
	FuSteelseriesSonicWirelessStatus wl_status;
	g_autoptr(FwupdRequest) request = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *msg = NULL;

	if (!fu_steelseries_sonic_wireless_status(self, &wl_status, error)) {
		g_prefix_error(error, "failed to get wireless status: ");
		return FALSE;
	}
	g_debug("WirelessStatus: %s", fu_steelseries_sonic_wireless_status_to_string(wl_status));
	if (wl_status == FU_STEELSERIES_SONIC_WIRELESS_STATUS_CONNECTED) {
		/* success */
		return TRUE;
	}

	/* the user has to do something */
	msg = g_strdup_printf("%s needs to be connected to start the update. "
			      "Please put the switch button underneath to 2.4G, or "
			      "click on any button to reconnect it.",
			      fu_device_get_name(FU_DEVICE(self)));
	request = fwupd_request_new();
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_PRESS_UNLOCK);
	fwupd_request_set_message(request, msg);
	if (!fu_device_emit_request(FU_DEVICE(self), request, progress, error))
		return FALSE;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_steelseries_sonic_wait_for_connect_cb,
				  delay / 1000,
				  1000,
				  &wl_status,
				  &error_local))
		g_debug("%s", error_local->message);
	if (wl_status != FU_STEELSERIES_SONIC_WIRELESS_STATUS_CONNECTED) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NEEDS_USER_ACTION, msg);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSteelseriesSonic *self = FU_STEELSERIES_SONIC(device);
	FuSteelseriesSonicChip chip;
	g_autoptr(FwupdRequest) request = NULL;
	g_autofree gchar *msg = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 50, "mouse");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 50, "holtek");

	/* mouse */
	chip = FU_STEELSERIES_SONIC_CHIP_MOUSE;
	if (!fu_steelseries_sonic_restart(self, chip, fu_progress_get_child(progress), error)) {
		g_prefix_error(error, "failed to restart chip %u: ", chip);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* USB receiver (nordic, holtek; same command) */
	chip = FU_STEELSERIES_SONIC_CHIP_HOLTEK;
	if (!fu_steelseries_sonic_restart(self, chip, fu_progress_get_child(progress), error)) {
		g_prefix_error(error, "failed to restart chip %u: ", chip);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* the user has to do something */
	msg = g_strdup_printf("%s needs to be manually restarted to complete the update. "
			      "Please unplug the 2.4G USB Wireless adapter and then re-plug it.",
			      fu_device_get_name(device));
	request = fwupd_request_new();
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_REPLUG);
	fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fwupd_request_set_message(request, msg);
	if (!fu_device_emit_request(FU_DEVICE(self), request, progress, error))
		return FALSE;

	/* success */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_steelseries_sonic_prepare(FuDevice *device,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuSteelseriesSonic *self = FU_STEELSERIES_SONIC(device);
	if (!fu_steelseries_sonic_wait_for_connect(self,
						   fu_device_get_remove_delay(FU_DEVICE(self)),
						   progress,
						   error))
		return FALSE;
	if (!fu_steelseries_sonic_ensure_battery_state(self, error)) {
		g_prefix_error(error, "failed to get battery state: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_write_chip(FuSteelseriesSonic *self,
				FuSteelseriesSonicChip chip,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	const guint8 *buf;
	gsize bufsz;
	g_autoptr(FuFirmware) fw = NULL;
	g_autoptr(GBytes) blob = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress,
			     FWUPD_STATUS_DEVICE_ERASE,
			     FU_STEELSERIES_SONIC_WRITE_PROGRESS_STEP_VALUE[chip][0],
			     NULL);
	fu_progress_add_step(progress,
			     FWUPD_STATUS_DEVICE_WRITE,
			     FU_STEELSERIES_SONIC_WRITE_PROGRESS_STEP_VALUE[chip][1],
			     NULL);

	fw = fu_firmware_get_image_by_id(firmware, FU_STEELSERIES_SONIC_FIRMWARE_ID[chip], error);
	if (fw == NULL)
		return FALSE;
	blob = fu_firmware_get_bytes(fw, error);
	if (blob == NULL)
		return FALSE;
	buf = fu_bytes_get_data_safe(blob, &bufsz, error);
	if (buf == NULL)
		return FALSE;
	if (!fu_steelseries_sonic_erase(self, chip, fu_progress_get_child(progress), error)) {
		g_prefix_error(error, "failed to erase chip %u: ", chip);
		return FALSE;
	}
	fu_progress_step_done(progress);
	if (!fu_steelseries_sonic_write_to_flash(self,
						 chip,
						 0x0,
						 blob,
						 fu_progress_get_child(progress),
						 error)) {
		g_prefix_error(error, "failed to write to flash chip %u: ", chip);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_steelseries_sonic_read_chip(FuSteelseriesSonic *self,
			       FuSteelseriesSonicChip chip,
			       FuProgress *progress,
			       GError **error)
{
	gsize bufsz;
	g_autoptr(GBytes) blob = NULL;
	g_autofree guint8 *buf = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, 1);

	bufsz = FU_STEELSERIES_SONIC_FIRMWARE_SIZE[chip];
	buf = g_malloc0(bufsz);
	if (!fu_steelseries_sonic_read_from_flash(self,
						  chip,
						  0x0,
						  buf,
						  bufsz,
						  fu_progress_get_child(progress),
						  error)) {
		g_prefix_error(error, "failed to read from flash chip %u: ", chip);
		return NULL;
	}
	fu_progress_step_done(progress);

	blob = g_bytes_new_take(g_steal_pointer(&buf), bufsz);
	return fu_firmware_new_from_bytes(blob);
}

static gboolean
fu_steelseries_sonic_verify_chip(FuSteelseriesSonic *self,
				 FuSteelseriesSonicChip chip,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 GError **error)
{
	g_autoptr(FuFirmware) fw_tmp = NULL;
	g_autoptr(FuFirmware) fw = NULL;
	g_autoptr(GBytes) blob_tmp = NULL;
	g_autoptr(GBytes) blob = NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 100, NULL);

	fw = fu_firmware_get_image_by_id(firmware, FU_STEELSERIES_SONIC_FIRMWARE_ID[chip], error);
	if (fw == NULL)
		return FALSE;
	blob = fu_firmware_get_bytes(fw, error);
	if (blob == NULL)
		return FALSE;
	fw_tmp = fu_steelseries_sonic_read_chip(self, chip, fu_progress_get_child(progress), error);
	if (fw_tmp == NULL) {
		g_prefix_error(error, "failed to read from flash chip %u: ", chip);
		return FALSE;
	}
	blob_tmp = fu_firmware_get_bytes(fw_tmp, error);
	if (blob_tmp == NULL)
		return FALSE;
	if (!fu_bytes_compare(blob_tmp, blob, error)) {
		fu_dump_raw(G_LOG_DOMAIN,
			    "Verify",
			    g_bytes_get_data(blob_tmp, NULL),
			    g_bytes_get_size(blob_tmp));
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_steelseries_sonic_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSteelseriesSonic *self = FU_STEELSERIES_SONIC(device);
	FuSteelseriesSonicChip chip;
	g_autoptr(FuFirmware) firmware = fu_archive_firmware_new();
	g_autoptr(FuFirmware) firmware_nordic = NULL;
	g_autoptr(FuFirmware) firmware_holtek = NULL;
	g_autoptr(FuFirmware) firmware_mouse = NULL;

	if (!fu_steelseries_sonic_wait_for_connect(self,
						   fu_device_get_remove_delay(device),
						   progress,
						   error))
		return NULL;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 18, "nordic");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 8, "holtek");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 73, "mouse");

	fu_archive_firmware_set_format(FU_ARCHIVE_FIRMWARE(firmware), FU_ARCHIVE_FORMAT_ZIP);
	fu_archive_firmware_set_compression(FU_ARCHIVE_FIRMWARE(firmware),
					    FU_ARCHIVE_COMPRESSION_NONE);

	/* nordic */
	chip = FU_STEELSERIES_SONIC_CHIP_NORDIC;
	firmware_nordic =
	    fu_steelseries_sonic_read_chip(self, chip, fu_progress_get_child(progress), error);
	if (firmware_nordic == NULL)
		return NULL;
	fu_firmware_set_id(firmware_nordic, FU_STEELSERIES_SONIC_FIRMWARE_ID[chip]);
	fu_firmware_add_image(firmware, firmware_nordic);
	fu_progress_step_done(progress);

	/* holtek */
	chip = FU_STEELSERIES_SONIC_CHIP_HOLTEK;
	firmware_holtek =
	    fu_steelseries_sonic_read_chip(self, chip, fu_progress_get_child(progress), error);
	if (firmware_holtek == NULL)
		return NULL;
	fu_firmware_set_id(firmware_holtek, FU_STEELSERIES_SONIC_FIRMWARE_ID[chip]);
	fu_firmware_add_image(firmware, firmware_holtek);
	fu_progress_step_done(progress);

	/* mouse */
	chip = FU_STEELSERIES_SONIC_CHIP_MOUSE;
	firmware_mouse =
	    fu_steelseries_sonic_read_chip(self, chip, fu_progress_get_child(progress), error);
	if (firmware_mouse == NULL)
		return NULL;
	fu_firmware_set_id(firmware_mouse, FU_STEELSERIES_SONIC_FIRMWARE_ID[chip]);
	fu_firmware_add_image(firmware, firmware_mouse);
	fu_progress_step_done(progress);

	/* success */
	fu_firmware_set_id(firmware, FU_FIRMWARE_ID_PAYLOAD);
	return g_steal_pointer(&firmware);
}

static gboolean
fu_steelseries_sonic_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuSteelseriesSonic *self = FU_STEELSERIES_SONIC(device);
	FuSteelseriesSonicChip chip;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 34, "device-write-mouse");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 30, "device-verify-mouse");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 17, "device-write-nordic");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 7, "device-verify-nordic");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 8, "device-write-holtek");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 3, "device-verify-holtek");

	/* mouse */
	chip = FU_STEELSERIES_SONIC_CHIP_MOUSE;
	if (!fu_steelseries_sonic_write_chip(self,
					     chip,
					     firmware,
					     fu_progress_get_child(progress),
					     flags,
					     error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_steelseries_sonic_verify_chip(self,
					      chip,
					      firmware,
					      fu_progress_get_child(progress),
					      error))
		return FALSE;
	fu_progress_step_done(progress);

	/* nordic */
	chip = FU_STEELSERIES_SONIC_CHIP_NORDIC;
	if (!fu_steelseries_sonic_write_chip(self,
					     chip,
					     firmware,
					     fu_progress_get_child(progress),
					     flags,
					     error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_steelseries_sonic_verify_chip(self,
					      chip,
					      firmware,
					      fu_progress_get_child(progress),
					      error))
		return FALSE;
	fu_progress_step_done(progress);

	/* holtek */
	chip = FU_STEELSERIES_SONIC_CHIP_HOLTEK;
	if (!fu_steelseries_sonic_write_chip(self,
					     FU_STEELSERIES_SONIC_CHIP_HOLTEK,
					     firmware,
					     fu_progress_get_child(progress),
					     flags,
					     error))
		return FALSE;
	fu_progress_step_done(progress);
	if (!fu_steelseries_sonic_verify_chip(self,
					      chip,
					      firmware,
					      fu_progress_get_child(progress),
					      error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_sonic_parse_firmware(FuFirmware *firmware,
				    FuFirmwareParseFlags flags,
				    GError **error)
{
	guint32 checksum_tmp;
	guint32 checksum;
	g_autoptr(GBytes) blob = NULL;

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;

	if (!fu_memread_uint32_safe(g_bytes_get_data(blob, NULL),
				    g_bytes_get_size(blob),
				    g_bytes_get_size(blob) - sizeof(checksum),
				    &checksum,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	checksum_tmp = fu_crc32(FU_CRC_KIND_B32_STANDARD,
				g_bytes_get_data(blob, NULL),
				g_bytes_get_size(blob) - sizeof(checksum_tmp));
	checksum_tmp = ~checksum_tmp;
	if (checksum_tmp != checksum) {
		if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "checksum mismatch for %s, got 0x%08x, expected 0x%08x",
				    fu_firmware_get_id(firmware),
				    checksum_tmp,
				    checksum);
			return FALSE;
		}
		g_debug("ignoring checksum mismatch, got 0x%08x, expected 0x%08x",
			checksum_tmp,
			checksum);
	}

	fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_HAS_CHECKSUM);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_steelseries_sonic_prepare_firmware(FuDevice *device,
				      GInputStream *stream,
				      FuProgress *progress,
				      FuFirmwareParseFlags flags,
				      GError **error)
{
	g_autoptr(FuFirmware) firmware_nordic = NULL;
	g_autoptr(FuFirmware) firmware_holtek = NULL;
	g_autoptr(FuFirmware) firmware_mouse = NULL;
	g_autoptr(FuFirmware) firmware = NULL;

	firmware = fu_archive_firmware_new();
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* mouse */
	firmware_mouse = fu_firmware_get_image_by_id(
	    firmware,
	    FU_STEELSERIES_SONIC_FIRMWARE_ID[FU_STEELSERIES_SONIC_CHIP_MOUSE],
	    error);
	if (firmware_mouse == NULL)
		return NULL;
	if (!fu_steelseries_sonic_parse_firmware(firmware_mouse, flags, error))
		return NULL;

	/* nordic */
	firmware_nordic = fu_firmware_get_image_by_id(
	    firmware,
	    FU_STEELSERIES_SONIC_FIRMWARE_ID[FU_STEELSERIES_SONIC_CHIP_NORDIC],
	    error);
	if (firmware_nordic == NULL)
		return NULL;
	if (!fu_steelseries_sonic_parse_firmware(firmware_nordic, flags, error))
		return NULL;

	/* holtek */
	firmware_holtek = fu_firmware_get_image_by_id(
	    firmware,
	    FU_STEELSERIES_SONIC_FIRMWARE_ID[FU_STEELSERIES_SONIC_CHIP_HOLTEK],
	    error);
	if (firmware_holtek == NULL)
		return NULL;
	if (!fu_steelseries_sonic_parse_firmware(firmware_holtek, flags, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
}

static void
fu_steelseries_sonic_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 92, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 3, "reload");
}

static void
fu_steelseries_sonic_class_init(FuSteelseriesSonicClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->attach = fu_steelseries_sonic_attach;
	device_class->prepare = fu_steelseries_sonic_prepare;
	device_class->read_firmware = fu_steelseries_sonic_read_firmware;
	device_class->write_firmware = fu_steelseries_sonic_write_firmware;
	device_class->prepare_firmware = fu_steelseries_sonic_prepare_firmware;
	device_class->set_progress = fu_steelseries_sonic_set_progress;
}

static void
fu_steelseries_sonic_init(FuSteelseriesSonic *self)
{
	fu_steelseries_device_set_iface_idx_offset(FU_STEELSERIES_DEVICE(self), -1);

	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_BCD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fu_device_add_protocol(FU_DEVICE(self), "com.steelseries.sonic");
	fu_device_set_install_duration(FU_DEVICE(self), 120);				 /* 2 min */
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_USER_REPLUG); /* 40 s */
	fu_device_set_battery_level(FU_DEVICE(self), FWUPD_BATTERY_LEVEL_INVALID);
	fu_device_set_battery_threshold(FU_DEVICE(self), 20);
}
