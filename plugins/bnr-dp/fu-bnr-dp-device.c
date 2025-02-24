/*
 * Copyright 2024 B&R Industrial Automation GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-bnr-dp-common.h"
#include "fu-bnr-dp-device.h"
#include "fu-bnr-dp-firmware.h"
#include "fu-bnr-dp-struct.h"

#define FU_BNR_DP_DEVICE_HEADER_OFFSET 0x00A00
#define FU_BNR_DP_DEVICE_DATA_OFFSET   0x00900

#define FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE 256
#define FU_BNR_DP_DEVICE_FLASH_PAGE_SIZE 65536

/* timeout in ms for aux reads/writes */
#define FU_BNR_DP_DEVICE_DPAUX_TIMEOUT_MSEC 3000

/* maximum number of polls to attempt without delay and in total. some commands will finish pretty
 * quickly, but more elaborate commands can take some time and a delay becomes appropriate when
 * polling */
#define FU_BNR_DP_DEVICE_POLL_MAX_FAST	    10
#define FU_BNR_DP_DEVICE_POLL_MAX_TOTAL	    100
#define FU_BNR_DP_DEVICE_POLL_INTERVAL_MSEC 5

struct _FuBnrDpDevice {
	FuDpauxDevice parent_instance;
};

G_DEFINE_TYPE(FuBnrDpDevice, fu_bnr_dp_device, FU_TYPE_DPAUX_DEVICE)

static guint8
fu_bnr_dp_device_xor_checksum(guint8 init, const guint8 *buf, gsize bufsz)
{
	for (gsize i = 0; i < bufsz; i++)
		init ^= buf[i];

	return init;
}

static FuStructBnrDpAuxRequest *
fu_bnr_dp_device_build_request(FuBnrDpOpcodes opcode,
			       FuBnrDpModuleNumber module_number,
			       guint16 offset,
			       guint16 data_len,
			       GError **error)
{
	g_autoptr(FuStructBnrDpAuxRequest) st_request = fu_struct_bnr_dp_aux_request_new();
	g_autoptr(FuStructBnrDpAuxCommand) st_command = fu_struct_bnr_dp_aux_command_new();

	fu_struct_bnr_dp_aux_command_set_module_number(st_command, module_number);
	fu_struct_bnr_dp_aux_command_set_opcode(st_command, opcode);

	if (!fu_struct_bnr_dp_aux_request_set_command(st_request, st_command, error))
		return NULL;
	fu_struct_bnr_dp_aux_request_set_data_len(st_request, data_len);
	fu_struct_bnr_dp_aux_request_set_offset(st_request, offset);

	return g_steal_pointer(&st_request);
}

/* evaluate the status from a response from the controller into an appropriate bool/GError */
static gboolean
fu_bnr_dp_device_eval_result(const FuStructBnrDpAuxStatus *st_status, GError **error)
{
	guint8 error_byte = fu_struct_bnr_dp_aux_status_get_error(st_status);
	guint8 error_code = error_byte & 0x0F;

	if (error_byte & FU_BNR_DP_AUX_STATUS_FLAGS_ERROR) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "device command failed with error '%s'",
			    fu_bnr_dp_aux_error_to_string(error_code) ?: "(invalid error code)");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_bnr_dp_device_is_done(const FuStructBnrDpAuxStatus *st_status, GError **error)
{
	guint8 error_byte = fu_struct_bnr_dp_aux_status_get_error(st_status);

	if (error_byte & FU_BNR_DP_AUX_STATUS_FLAGS_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "device is busy");
		return FALSE;
	}

	return TRUE;
}

/* Write a single `request` and some optional data to the device. */
static gboolean
fu_bnr_dp_device_write_request(FuBnrDpDevice *self,
			       const FuStructBnrDpAuxRequest *st_request,
			       const guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	guint8 checksum = fu_bnr_dp_device_xor_checksum(FU_BNR_DP_CHECKSUM_INIT_TX,
							st_request->data,
							st_request->len);
	g_autoptr(FuStructBnrDpAuxTxHeader) st_header = fu_struct_bnr_dp_aux_tx_header_new();

	if (!fu_struct_bnr_dp_aux_tx_header_set_request(st_header, st_request, error))
		return FALSE;

	/* write optional data */
	if (buf != NULL && bufsz > 0) {
		if (!fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
					   FU_BNR_DP_DEVICE_DATA_OFFSET,
					   buf,
					   bufsz,
					   FU_BNR_DP_DEVICE_DPAUX_TIMEOUT_MSEC,
					   error))
			return FALSE;

		checksum = fu_bnr_dp_device_xor_checksum(checksum, buf, bufsz);
	}

	fu_struct_bnr_dp_aux_tx_header_set_checksum(st_header, checksum);

	/* write header to kick off processing by the device */
	return fu_dpaux_device_write(FU_DPAUX_DEVICE(self),
				     FU_BNR_DP_DEVICE_HEADER_OFFSET,
				     st_header->data,
				     st_header->len,
				     FU_BNR_DP_DEVICE_DPAUX_TIMEOUT_MSEC,
				     error);
}

/* read a single `response` and some optional data from the device after a finished command. reading
 * the full 7 byte header from the header offset returns a different structure than when reading
 * only 2 bytes */
static gboolean
fu_bnr_dp_device_read_response(FuBnrDpDevice *self, GByteArray *data, GError **error)
{
	guint8 actual_checksum;
	guint8 tmp[FU_STRUCT_BNR_DP_AUX_RX_HEADER_SIZE] = {0};
	g_autoptr(FuStructBnrDpAuxRxHeader) st_header = NULL;
	g_autoptr(FuStructBnrDpAuxResponse) st_response = NULL;

	/* read full header once command has finished */
	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  FU_BNR_DP_DEVICE_HEADER_OFFSET,
				  tmp,
				  sizeof(tmp),
				  FU_BNR_DP_DEVICE_DPAUX_TIMEOUT_MSEC,
				  error))
		return FALSE;

	st_header = fu_struct_bnr_dp_aux_rx_header_parse(tmp, sizeof(tmp), 0, error);
	if (st_header == NULL)
		return FALSE;

	st_response = fu_struct_bnr_dp_aux_rx_header_get_response(st_header);
	if (st_response == NULL)
		return FALSE;

	actual_checksum = fu_bnr_dp_device_xor_checksum(FU_BNR_DP_CHECKSUM_INIT_RX,
							st_response->data,
							st_response->len);

	/* read command output data */
	g_byte_array_set_size(data, fu_struct_bnr_dp_aux_response_get_data_len(st_response));
	if (data->len > 0) {
		if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
					  FU_BNR_DP_DEVICE_DATA_OFFSET,
					  data->data,
					  data->len,
					  FU_BNR_DP_DEVICE_DPAUX_TIMEOUT_MSEC,
					  error))
			return FALSE;

		actual_checksum =
		    fu_bnr_dp_device_xor_checksum(actual_checksum, data->data, data->len);
	}

	if (actual_checksum != fu_struct_bnr_dp_aux_rx_header_get_checksum(st_header)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "checksum mismatch in device response header (header specified: 0x%X, "
			    "actual: 0x%X)",
			    fu_struct_bnr_dp_aux_rx_header_get_checksum(st_header),
			    actual_checksum);
		return FALSE;
	}

	return TRUE;
}

/* read only 2 bytes from the header offset to receive the status */
static FuStructBnrDpAuxStatus *
fu_bnr_dp_device_read_status(FuBnrDpDevice *self, GError **error)
{
	guint8 buf[FU_STRUCT_BNR_DP_AUX_STATUS_SIZE] = {0};
	g_autoptr(FuStructBnrDpAuxStatus) st_status = NULL;

	/* only read the first 2 bytes of the header to check status bits. */
	if (!fu_dpaux_device_read(FU_DPAUX_DEVICE(self),
				  FU_BNR_DP_DEVICE_HEADER_OFFSET,
				  buf,
				  sizeof(buf),
				  FU_BNR_DP_DEVICE_DPAUX_TIMEOUT_MSEC,
				  error))
		return NULL;

	st_status = fu_struct_bnr_dp_aux_status_parse(buf, sizeof(buf), 0, error);
	if (st_status == NULL)
		return NULL;

	return g_steal_pointer(&st_status);
}

static gboolean
fu_bnr_dp_device_poll_status_cb(FuDevice *device, gpointer user_data, GError **error)
{
	g_autoptr(FuStructBnrDpAuxStatus) st_status = NULL;

	st_status = fu_bnr_dp_device_read_status(FU_BNR_DP_DEVICE(device), error);
	if (st_status == NULL)
		return FALSE;

	if (!fu_bnr_dp_device_eval_result(st_status, error))
		return FALSE;

	return fu_bnr_dp_device_is_done(st_status, error);
}

static gboolean
fu_bnr_dp_device_poll_status(FuBnrDpDevice *self, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_bnr_dp_device_poll_status_cb,
				    FU_BNR_DP_DEVICE_POLL_MAX_FAST,
				    0,
				    NULL,
				    NULL) ||
	       fu_device_retry_full(FU_DEVICE(self),
				    fu_bnr_dp_device_poll_status_cb,
				    FU_BNR_DP_DEVICE_POLL_MAX_TOTAL -
					FU_BNR_DP_DEVICE_POLL_MAX_FAST,
				    FU_BNR_DP_DEVICE_POLL_INTERVAL_MSEC,
				    NULL,
				    error);
}

static GByteArray *
fu_bnr_dp_device_exec_cmd(FuBnrDpDevice *self,
			  FuBnrDpOpcodes opcode,
			  FuBnrDpModuleNumber module_number,
			  guint16 offset,
			  GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(FuStructBnrDpAuxRequest) st_request = NULL;

	st_request = fu_bnr_dp_device_build_request(opcode, module_number, offset, 0, error);
	if (st_request == NULL)
		return NULL;

	if (!fu_bnr_dp_device_write_request(self, st_request, NULL, 0, error))
		return NULL;

	if (!fu_bnr_dp_device_poll_status(self, error)) {
		g_prefix_error(error,
			       "command %s to module %s at offset 0x%X: ",
			       fu_bnr_dp_opcodes_to_string(opcode),
			       fu_bnr_dp_module_number_to_string(module_number),
			       offset);
		return NULL;
	}

	if (!fu_bnr_dp_device_read_response(self, buf, error))
		return NULL;

	return g_steal_pointer(&buf);
}

static GByteArray *
fu_bnr_dp_device_read_data(FuBnrDpDevice *self,
			   FuBnrDpOpcodes opcode,
			   FuBnrDpModuleNumber module_number,
			   gsize offset,
			   gsize size,
			   FuProgress *progress,
			   GError **error)
{
	const guint16 start = offset / FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE;
	const guint16 end = (offset + size) / FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE;
	g_autoptr(GByteArray) buf = g_byte_array_sized_new(size);

	g_return_val_if_fail(offset % FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE == 0, NULL);
	g_return_val_if_fail(size % FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE == 0, NULL);
	g_return_val_if_fail(start < end, NULL);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, end - start);

	for (guint16 idx = start; idx < end; idx++) {
		g_autoptr(GByteArray) chunk = NULL;

		chunk = fu_bnr_dp_device_exec_cmd(self, opcode, module_number, idx, error);
		if (chunk == NULL)
			return NULL;
		g_byte_array_append(buf, chunk->data, chunk->len);

		fu_progress_step_done(progress);
	}

	return g_steal_pointer(&buf);
}

/* check if the current chunk can be skipped. this is a flash optimization. writing to start of page
 * erases full block and allows us to skip further writes to that page if the chunk is entirely
 * 0xff */
static gboolean
fu_bnr_dp_device_can_skip_chunk(const guint8 *buf, gsize bufsz, gsize cur_offset)
{
	g_return_val_if_fail(cur_offset + FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE <= bufsz, FALSE);

	/* can't skip the first chunk in a flash page */
	if ((cur_offset % FU_BNR_DP_DEVICE_FLASH_PAGE_SIZE) == 0)
		return FALSE;

	/* can't skip if any byte in the chunk is not 0xff */
	for (gsize i = cur_offset; i < cur_offset + FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE; i++) {
		if (buf[i] != 0xff)
			return FALSE;
	}

	/* can skip */
	return TRUE;
}

static gboolean
fu_bnr_dp_device_write_data(FuBnrDpDevice *self,
			    FuBnrDpOpcodes opcode,
			    FuBnrDpModuleNumber module_number,
			    gsize offset,
			    const guint8 *buf,
			    gsize bufsz,
			    FuProgress *progress,
			    GError **error)
{
	const guint16 start = offset / FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE;
	const guint16 end = (offset + bufsz) / FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE;
	g_autoptr(FuStructBnrDpAuxRequest) st_request = NULL;

	g_return_val_if_fail(offset % FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE == 0, FALSE);
	g_return_val_if_fail(bufsz % FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE == 0, FALSE);
	g_return_val_if_fail(start < end, FALSE);

	st_request = fu_bnr_dp_device_build_request(opcode,
						    module_number,
						    0,
						    FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE,
						    error);
	if (st_request == NULL)
		return FALSE;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, end - start);

	for (guint16 idx = start; idx < end; idx++) {
		const gsize cur_offset = idx * FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE;

		if (fu_bnr_dp_device_can_skip_chunk(buf, bufsz, cur_offset)) {
			fu_progress_step_done(progress);
			continue;
		}

		fu_struct_bnr_dp_aux_request_set_offset(st_request, idx);
		if (!fu_bnr_dp_device_write_request(self,
						    st_request,
						    &buf[cur_offset],
						    FU_BNR_DP_DEVICE_DATA_CHUNK_SIZE,
						    error))
			return FALSE;

		if (!fu_bnr_dp_device_poll_status(self, error)) {
			g_prefix_error(error,
				       "command %s to module %s at offset 0x%X: ",
				       fu_bnr_dp_opcodes_to_string(opcode),
				       fu_bnr_dp_module_number_to_string(module_number),
				       idx);
			return FALSE;
		}

		fu_progress_step_done(progress);
	}

	return TRUE;
}

static FuStructBnrDpPayloadHeader *
fu_bnr_dp_device_factory_data(FuBnrDpDevice *self,
			      FuBnrDpModuleNumber module_number,
			      GError **error)
{
	g_autoptr(GByteArray) output = NULL;

	output = fu_bnr_dp_device_exec_cmd(self,
					   FU_BNR_DP_OPCODES_FACTORY_DATA,
					   module_number,
					   0x0,
					   error);
	if (output == NULL)
		return NULL;

	return fu_struct_bnr_dp_factory_data_parse(output->data, output->len, 0, error);
}

/* read the fw header for the currently active firmware */
static FuStructBnrDpPayloadHeader *
fu_bnr_dp_device_fw_header(FuBnrDpDevice *self, FuBnrDpModuleNumber module_number, GError **error)
{
	g_autoptr(GByteArray) output = NULL;

	output = fu_bnr_dp_device_exec_cmd(self,
					   FU_BNR_DP_OPCODES_FLASH_SAVE_HEADER_INFO,
					   module_number,
					   0x0,
					   error);
	if (output == NULL)
		return NULL;

	return fu_struct_bnr_dp_payload_header_parse(output->data, output->len, 0, error);
}

static gboolean
fu_bnr_dp_device_reset(FuBnrDpDevice *self, FuBnrDpModuleNumber module_number, GError **error)
{
	g_autoptr(FuStructBnrDpAuxRequest) st_request = NULL;

	st_request = fu_bnr_dp_device_build_request(FU_BNR_DP_OPCODES_RESET,
						    module_number,
						    0xDEAD,
						    0,
						    error);
	if (st_request == NULL)
		return FALSE;

	return fu_bnr_dp_device_write_request(self, st_request, NULL, 0, error);
}

static gboolean
fu_bnr_dp_device_setup(FuDevice *device, GError **error)
{
	FuBnrDpDevice *self = FU_BNR_DP_DEVICE(device);
	guint64 version = 0;
	g_autofree gchar *version_str = NULL;
	g_autofree gchar *id_str = NULL;
	g_autofree gchar *serial = NULL;
	g_autofree gchar *hw_rev = NULL;
	g_autofree gchar *oui = NULL;
	g_autoptr(FuStructBnrDpPayloadHeader) st_header = NULL;
	g_autoptr(FuStructBnrDpFactoryData) st_factory_data = NULL;

	/* DpauxDevice->setup */
	if (!FU_DEVICE_CLASS(fu_bnr_dp_device_parent_class)->setup(device, error))
		return FALSE;

	st_header = fu_bnr_dp_device_fw_header(self, FU_BNR_DP_MODULE_NUMBER_RECEIVER, error);
	if (st_header == NULL)
		return FALSE;
	st_factory_data =
	    fu_bnr_dp_device_factory_data(self, FU_BNR_DP_MODULE_NUMBER_RECEIVER, error);
	if (st_factory_data == NULL)
		return FALSE;

	/* convert from string encoded version to integer and back to a nicer string format */
	if (!fu_bnr_dp_version_from_header(st_header, &version, error))
		return FALSE;
	version_str = fu_bnr_dp_version_to_string(version);
	fu_device_set_version(device, version_str);

	id_str = fu_struct_bnr_dp_factory_data_get_identification(st_factory_data);
	if (id_str == NULL)
		return FALSE;
	fu_device_set_name(FU_DEVICE(self), id_str);

	serial = fu_struct_bnr_dp_factory_data_get_serial(st_factory_data);
	if (serial == NULL)
		return FALSE;
	fu_device_set_serial(device, serial);

	fu_device_add_instance_u32(device, "DEV", fu_bnr_dp_effective_product_num(st_factory_data));
	fu_device_add_instance_u32(device,
				   "VARIANT",
				   fu_bnr_dp_effective_compat_id(st_factory_data));

	hw_rev = fu_struct_bnr_dp_factory_data_get_hw_rev(st_factory_data);
	if (hw_rev == NULL)
		return FALSE;
	fu_device_add_instance_str(device, "HW_REV", hw_rev);

	oui = g_strdup_printf("%06X", fu_dpaux_device_get_dpcd_ieee_oui(FU_DPAUX_DEVICE(device)));
	fu_device_build_vendor_id(FU_DEVICE(self), "OUI", oui);
	return fu_device_build_instance_id(device,
					   error,
					   "DPAUX",
					   "OUI",
					   "DEV",
					   "VARIANT",
					   "HW_REV",
					   NULL);
}

static FuFirmware *
fu_bnr_dp_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuBnrDpDevice *self = FU_BNR_DP_DEVICE(device);
	FuBnrDpPayloadFlags flags;
	gsize offset = FU_BNR_DP_FIRMWARE_SIZE;
	guint16 crc;
	g_autoptr(FuFirmware) firmware = fu_bnr_dp_firmware_new();
	g_autoptr(GByteArray) image = NULL;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(FuStructBnrDpFactoryData) st_factory_data = NULL;
	g_autoptr(FuStructBnrDpPayloadHeader) st_header = NULL;

	st_factory_data =
	    fu_bnr_dp_device_factory_data(self, FU_BNR_DP_MODULE_NUMBER_RECEIVER, error);
	if (st_factory_data == NULL)
		return NULL;
	st_header = fu_bnr_dp_device_fw_header(self, FU_BNR_DP_MODULE_NUMBER_RECEIVER, error);
	if (st_header == NULL)
		return NULL;

	flags = fu_struct_bnr_dp_payload_header_get_flags(st_header);

	/* the flash is 3 * `FU_BNR_DP_FW_SIZE`; first third is boot loader, then low and high
	 * images */
	if ((flags & FU_BNR_DP_PAYLOAD_FLAGS_BOOT_AREA) == FU_BNR_DP_BOOT_AREA_HIGH)
		offset *= 2;

	image = fu_bnr_dp_device_read_data(self,
					   FU_BNR_DP_OPCODES_FLASH_SERVICE,
					   FU_BNR_DP_MODULE_NUMBER_RECEIVER,
					   offset,
					   FU_BNR_DP_FIRMWARE_SIZE,
					   progress,
					   error);
	if (image == NULL)
		return NULL;

	crc = fu_crc16(FU_CRC_KIND_B16_BNR, image->data, image->len);
	if (crc != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "CRC mismatch in read firmware image: 0x%Xu",
			    crc);
		return NULL;
	}

	bytes = g_bytes_new(image->data, image->len);
	if (bytes == NULL)
		return NULL;
	fu_firmware_set_bytes(firmware, bytes);

	/* populate private data to be able to build an XML header if `firmware->write()` is used */
	if (!fu_bnr_dp_firmware_parse_from_device(FU_BNR_DP_FIRMWARE(firmware),
						  st_factory_data,
						  st_header,
						  error))
		return NULL;

	return g_steal_pointer(&firmware);
}

static GBytes *
fu_bnr_dp_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuBnrDpDevice *self = FU_BNR_DP_DEVICE(device);
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_bnr_dp_device_read_data(self,
					 FU_BNR_DP_OPCODES_FLASH_SERVICE,
					 FU_BNR_DP_MODULE_NUMBER_RECEIVER,
					 0,
					 FU_BNR_DP_FIRMWARE_SIZE * 3,
					 progress,
					 error);
	if (buf == NULL)
		return NULL;

	return g_bytes_new(buf->data, buf->len);
}

static FuFirmware *
fu_bnr_dp_device_prepare_firmware(FuDevice *device,
				  GInputStream *stream,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuBnrDpDevice *self = FU_BNR_DP_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_bnr_dp_firmware_new();
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(FuStructBnrDpFactoryData) st_factory_data = NULL;
	g_autoptr(FuStructBnrDpPayloadHeader) st_active_header = NULL;
	g_autoptr(FuStructBnrDpPayloadHeader) st_fw_header = NULL;

	/* parse to bnr-dp firmware */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* use bytes instead of stream to make patching work */
	bytes = fu_firmware_get_bytes(firmware, error);
	if (bytes == NULL)
		return NULL;
	fu_firmware_set_bytes(firmware, bytes);

	/* patch firmware boot counter to be higher than active image */
	st_active_header =
	    fu_bnr_dp_device_fw_header(self, FU_BNR_DP_MODULE_NUMBER_RECEIVER, error);
	if (st_active_header == NULL)
		return NULL;
	if (!fu_bnr_dp_firmware_patch_boot_counter(
		FU_BNR_DP_FIRMWARE(firmware),
		fu_struct_bnr_dp_payload_header_get_counter(st_active_header),
		error))
		return NULL;

	/* check fw image */
	st_factory_data =
	    fu_bnr_dp_device_factory_data(self, FU_BNR_DP_MODULE_NUMBER_RECEIVER, error);
	if (st_factory_data == NULL)
		return NULL;
	st_fw_header = fu_struct_bnr_dp_payload_header_parse(g_bytes_get_data(bytes, NULL),
							     g_bytes_get_size(bytes),
							     FU_BNR_DP_FIRMWARE_HEADER_OFFSET,
							     error);
	if (st_fw_header == NULL)
		return NULL;
	if (!fu_bnr_dp_firmware_check(FU_BNR_DP_FIRMWARE(firmware),
				      st_factory_data,
				      st_active_header,
				      st_fw_header,
				      flags,
				      error))
		return NULL;

	return g_steal_pointer(&firmware);
}

static gboolean
fu_bnr_dp_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuBnrDpDevice *self = FU_BNR_DP_DEVICE(device);
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GByteArray) read_back = NULL;

	/* progress, values based on dev tests with -vv */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 32, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 67, "verify");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, "activate");

	/* get payload bytes including patched boot counter */
	bytes = fu_firmware_get_bytes_with_patches(firmware, error);
	if (bytes == NULL)
		return FALSE;

	/* write new firmware to inactive area */
	if (!fu_bnr_dp_device_write_data(self,
					 FU_BNR_DP_OPCODES_FLASH_USER,
					 FU_BNR_DP_MODULE_NUMBER_RECEIVER,
					 0,
					 g_bytes_get_data(bytes, NULL),
					 g_bytes_get_size(bytes),
					 fu_progress_get_child(progress),
					 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify written data */
	read_back = fu_bnr_dp_device_read_data(self,
					       FU_BNR_DP_OPCODES_FLASH_USER,
					       FU_BNR_DP_MODULE_NUMBER_RECEIVER,
					       0,
					       FU_BNR_DP_FIRMWARE_SIZE,
					       fu_progress_get_child(progress),
					       error);
	if (read_back == NULL)
		return FALSE;
	if (!fu_memcmp_safe(g_bytes_get_data(bytes, NULL),
			    g_bytes_get_size(bytes),
			    0,
			    read_back->data,
			    read_back->len,
			    0,
			    FU_BNR_DP_FIRMWARE_SIZE,
			    error)) {
		g_prefix_error_literal(error, "verification of written firmware failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* apply new firmware by resetting the device */
	if (!fu_bnr_dp_device_reset(self, FU_BNR_DP_MODULE_NUMBER_RECEIVER, error))
		return FALSE;
	/* give controller some time before ->reload() tries to read info again */
	fu_device_sleep(device, 3000);
	fu_progress_step_done(progress);

	return TRUE;
}

static gchar *
fu_bnr_dp_device_convert_version(FuDevice *self, guint64 version_raw)
{
	return fu_bnr_dp_version_to_string(version_raw);
}

static void
fu_bnr_dp_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_bnr_dp_device_init(FuBnrDpDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_vendor(FU_DEVICE(self), "B&R Industrial Automation GmbH");
	fu_device_add_protocol(FU_DEVICE(self), "com.br-automation.dpaux");
	fu_device_add_icon(FU_DEVICE(self), "video-display");
	fu_device_set_firmware_size_max(FU_DEVICE(self), FU_BNR_DP_FIRMWARE_SIZE_MAX);

	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	fu_device_retry_add_recovery(FU_DEVICE(self), FWUPD_ERROR, FWUPD_ERROR_READ, NULL);
}

static void
fu_bnr_dp_device_class_init(FuBnrDpDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	device_class->convert_version = fu_bnr_dp_device_convert_version;
	device_class->dump_firmware = fu_bnr_dp_device_dump_firmware;
	device_class->prepare_firmware = fu_bnr_dp_device_prepare_firmware;
	device_class->read_firmware = fu_bnr_dp_device_read_firmware;
	device_class->reload = fu_bnr_dp_device_setup;
	device_class->set_progress = fu_bnr_dp_device_set_progress;
	device_class->setup = fu_bnr_dp_device_setup;
	device_class->write_firmware = fu_bnr_dp_device_write_firmware;
}
