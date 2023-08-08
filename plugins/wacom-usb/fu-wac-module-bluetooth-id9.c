/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021-2023 Jason Gerecke <jason.gerecke@wacom.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-wac-common.h"
#include "fu-wac-device.h"
#include "fu-wac-module-bluetooth-id9.h"
#include "fu-wac-struct.h"

struct _FuWacModuleBluetoothId9 {
	FuWacModule parent_instance;
};

G_DEFINE_TYPE(FuWacModuleBluetoothId9, fu_wac_module_bluetooth_id9, FU_TYPE_WAC_MODULE)

#define FU_WAC_MODULE_BLUETOOTH_ID9_PAYLOAD_SZ	   256
#define FU_WAC_MODULE_BLUETOOTH_ID9_START_NORMAL   0x00
#define FU_WAC_MODULE_BLUETOOTH_ID9_CMD_NORMAL	   0x00
#define FU_WAC_MODULE_BLUETOOTH_ID9_CMD_FULLERASE  0xFE
#define FU_WAC_MODULE_BLUETOOTH_ID9_LOADER_RAM	   0x02
#define FU_WAC_MODULE_BLUETOOTH_ID9_LOADER_BEGIN   0x03
#define FU_WAC_MODULE_BLUETOOTH_ID9_LOADER_DATA	   0x04
#define FU_WAC_MODULE_BLUETOOTH_ID9_CRC_POLYNOMIAL 0xEDB88320

static FuChunk *
fu_wac_module_bluetooth_id9_get_startcmd(GBytes *data, gboolean full_erase, GError **error)
{
	guint8 command = full_erase ? FU_WAC_MODULE_BLUETOOTH_ID9_CMD_FULLERASE
				    : FU_WAC_MODULE_BLUETOOTH_ID9_CMD_NORMAL;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(data, &bufsz);
	guint32 crc;
	g_autoptr(GByteArray) loader_cmd = fu_struct_id9_loader_cmd_new();
	g_autoptr(GByteArray) spi_cmd = fu_struct_id9_spi_cmd_new();
	g_autoptr(GByteArray) unknown_cmd = fu_struct_id9_unknown_cmd_new();

	fu_struct_id9_unknown_cmd_set_size(unknown_cmd, bufsz);

	fu_struct_id9_spi_cmd_set_size(spi_cmd, bufsz + FU_STRUCT_ID9_UNKNOWN_CMD_SIZE);
	if (!fu_struct_id9_spi_cmd_set_data(spi_cmd, unknown_cmd, error))
		return NULL;

	fu_struct_id9_loader_cmd_set_command(loader_cmd, command);
	fu_struct_id9_loader_cmd_set_size(loader_cmd, bufsz + FU_STRUCT_ID9_SPI_CMD_SIZE);
	/* CRC(concat(spi_cmd, *data)) */
	crc = fu_crc32_full(spi_cmd->data,
			    FU_STRUCT_ID9_SPI_CMD_SIZE,
			    ~0,
			    FU_WAC_MODULE_BLUETOOTH_ID9_CRC_POLYNOMIAL);
	crc = fu_crc32_full(buf, bufsz, ~crc, FU_WAC_MODULE_BLUETOOTH_ID9_CRC_POLYNOMIAL);
	fu_struct_id9_loader_cmd_set_crc(loader_cmd, crc);
	if (!fu_struct_id9_loader_cmd_set_data(loader_cmd, spi_cmd, error))
		return NULL;

	if (!fu_struct_id9_loader_cmd_validate(loader_cmd->data, loader_cmd->len, 0, error))
		return NULL;

	return fu_chunk_bytes_new(g_bytes_new(loader_cmd->data, loader_cmd->len));
}

static gboolean
fu_wac_module_bluetooth_id9_write_block(FuWacModule *self,
					guint8 phase,
					FuChunk *chunk,
					FuProgress *progress,
					GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob_chunk = NULL;

	fu_byte_array_append_uint8(buf, phase);
	g_byte_array_append(buf, fu_chunk_get_data(chunk), fu_chunk_get_data_sz(chunk));

	blob_chunk = g_bytes_new(buf->data, buf->len);
	return fu_wac_module_set_feature(self,
					 FU_WAC_MODULE_COMMAND_DATA,
					 blob_chunk,
					 fu_progress_get_child(progress),
					 FU_WAC_MODULE_WRITE_TIMEOUT,
					 error);
}

static gboolean
fu_wac_module_bluetooth_id9_write_blocks(FuWacModule *self,
					 guint8 phase,
					 GBytes *data,
					 gsize block_len,
					 FuProgress *progress,
					 GError **error)
{
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new_from_bytes(data, 0, 0, block_len);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		if (!fu_wac_module_bluetooth_id9_write_block(self,
							     phase,
							     g_ptr_array_index(chunks, i),
							     progress,
							     error))
			return FALSE;

		fu_progress_step_done(progress);
	}
	return TRUE;
}

static FuFirmware *
fu_wac_module_bluetooth_id9_prepare_firmware(FuDevice *device,
					     GBytes *fw,
					     FwupdInstallFlags flags,
					     GError **error)
{
	const guint8 *blob;
	gsize blob_len = 0;
	guint16 loader_len = 0;
	gsize payload_len = 0;
	g_autoptr(GBytes) loader_bytes = NULL;
	g_autoptr(GBytes) payload_bytes = NULL;
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuFirmware) loader_fw = NULL;
	g_autoptr(FuFirmware) payload_fw = NULL;

	/* The firmware file is formatted as a 2 byte "length" field
	 * followed by <length> bytes of loader code. The remainder
	 * payload is firmware data.
	 */
	blob = g_bytes_get_data(fw, &blob_len);
	if (!fu_memread_uint16_safe(blob, blob_len, 0, &loader_len, G_BIG_ENDIAN, error)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid firmware size");
		return NULL;
	}

	if (loader_len > blob_len - 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid firmware loader size");
		return NULL;
	}
	loader_bytes = fu_bytes_new_offset(fw, 2, loader_len, error);
	if (loader_bytes == NULL)
		return NULL;
	loader_fw = fu_firmware_new_from_bytes(loader_bytes);
	fu_firmware_set_id(loader_fw, FU_FIRMWARE_ID_HEADER);
	fu_firmware_add_image(firmware, loader_fw);

	payload_len = blob_len - 2 - loader_len;
	payload_bytes = fu_bytes_new_offset(fw, 2 + loader_len, payload_len, error);
	if (payload_bytes == NULL)
		return NULL;
	payload_fw = fu_firmware_new_from_bytes(payload_bytes);
	fu_firmware_set_id(payload_fw, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_add_image(firmware, payload_fw);

	return g_steal_pointer(&firmware);
}

static gboolean
fu_wac_module_bluetooth_id9_write_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuWacModule *self = FU_WAC_MODULE(device);
	const guint8 buf_start[] = {FU_WAC_MODULE_BLUETOOTH_ID9_START_NORMAL};
	g_autoptr(GBytes) blob_start = g_bytes_new_static(buf_start, sizeof(buf_start));
	g_autoptr(GBytes) loader = NULL;
	g_autoptr(GBytes) payload = NULL;
	g_autoptr(FuChunk) cmd = NULL;

	/* get firmware images */
	loader = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_HEADER, error);
	if (loader == NULL)
		return FALSE;
	payload = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (payload == NULL)
		return FALSE;
	cmd = fu_wac_module_bluetooth_id9_get_startcmd(payload, FALSE, error);
	if (cmd == NULL)
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 22, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 67, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, NULL);

	/* start */
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_START,
				       blob_start,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_ERASE_TIMEOUT,
				       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* transfer flash programmer to device RAM */
	if (!fu_wac_module_bluetooth_id9_write_blocks(self,
						      FU_WAC_MODULE_BLUETOOTH_ID9_LOADER_RAM,
						      loader,
						      FU_WAC_MODULE_BLUETOOTH_ID9_PAYLOAD_SZ,
						      fu_progress_get_child(progress),
						      error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send "flash start" command to programer */
	if (!fu_wac_module_bluetooth_id9_write_block(self,
						     FU_WAC_MODULE_BLUETOOTH_ID9_LOADER_BEGIN,
						     cmd,
						     progress,
						     error))
		return FALSE;

	/* transfer payload for programming */
	if (!fu_wac_module_bluetooth_id9_write_blocks(self,
						      FU_WAC_MODULE_BLUETOOTH_ID9_LOADER_DATA,
						      payload,
						      FU_WAC_MODULE_BLUETOOTH_ID9_PAYLOAD_SZ,
						      fu_progress_get_child(progress),
						      error))
		return FALSE;
	fu_progress_step_done(progress);

	/* end */
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_END,
				       NULL,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_COMMIT_TIMEOUT,
				       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_wac_module_bluetooth_id9_init(FuWacModuleBluetoothId9 *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_install_duration(FU_DEVICE(self), 15);
}

static void
fu_wac_module_bluetooth_id9_class_init(FuWacModuleBluetoothId9Class *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->write_firmware = fu_wac_module_bluetooth_id9_write_firmware;
	klass_device->prepare_firmware = fu_wac_module_bluetooth_id9_prepare_firmware;
}

FuWacModule *
fu_wac_module_bluetooth_id9_new(FuDevice *proxy)
{
	FuWacModule *module = NULL;
	module = g_object_new(FU_TYPE_WAC_MODULE_BLUETOOTH_ID9,
			      "proxy",
			      proxy,
			      "fw-type",
			      FU_WAC_MODULE_FW_TYPE_BLUETOOTH_ID9,
			      NULL);
	return module;
}
