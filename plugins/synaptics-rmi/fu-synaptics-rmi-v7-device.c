/*
 * Copyright (C) 2012-2014 Andrew Duggan
 * Copyright (C) 2012-2019 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-chunk.h"
#include "fu-synaptics-rmi-v7-device.h"

#include "fwupd-error.h"

#define RMI_F34_ERASE_WAIT_MS				10000		/* ms */

typedef enum {
	RMI_FLASH_CMD_IDLE				= 0x00,
	RMI_FLASH_CMD_ENTER_BL,
	RMI_FLASH_CMD_READ,
	RMI_FLASH_CMD_WRITE,
	RMI_FLASH_CMD_ERASE,
	RMI_FLASH_CMD_ERASE_AP,
	RMI_FLASH_CMD_SENSOR_ID,
} RmiFlashCommand;

typedef enum {
	RMI_PARTITION_ID_NONE				= 0x00,
	RMI_PARTITION_ID_BOOTLOADER			= 0x01,
	RMI_PARTITION_ID_DEVICE_CONFIG,
	RMI_PARTITION_ID_FLASH_CONFIG,
	RMI_PARTITION_ID_MANUFACTURING_BLOCK,
	RMI_PARTITION_ID_GUEST_SERIALIZATION,
	RMI_PARTITION_ID_GLOBAL_PARAMETERS,
	RMI_PARTITION_ID_CORE_CODE,
	RMI_PARTITION_ID_CORE_CONFIG,
	RMI_PARTITION_ID_GUEST_CODE,
	RMI_PARTITION_ID_DISPLAY_CONFIG,
	RMI_PARTITION_ID_EXTERNAL_TOUCH_AFE_CONFIG,
	RMI_PARTITION_ID_UTILITY_PARAMETER,
} RmiPartitionId;

static const gchar *
rmi_firmware_partition_id_to_string (RmiPartitionId partition_id)
{
	if (partition_id == RMI_PARTITION_ID_NONE)
		return "none";
	if (partition_id == RMI_PARTITION_ID_BOOTLOADER)
		return "bootloader";
	if (partition_id == RMI_PARTITION_ID_DEVICE_CONFIG)
		return "device-config";
	if (partition_id == RMI_PARTITION_ID_FLASH_CONFIG)
		return "flash-config";
	if (partition_id == RMI_PARTITION_ID_MANUFACTURING_BLOCK)
		return "manufacturing-block";
	if (partition_id == RMI_PARTITION_ID_GUEST_SERIALIZATION)
		return "guest-serialization";
	if (partition_id == RMI_PARTITION_ID_GLOBAL_PARAMETERS)
		return "global-parameters";
	if (partition_id == RMI_PARTITION_ID_CORE_CODE)
		return "core-code";
	if (partition_id == RMI_PARTITION_ID_CORE_CONFIG)
		return "core-config";
	if (partition_id == RMI_PARTITION_ID_GUEST_CODE)
		return "guest-code";
	if (partition_id == RMI_PARTITION_ID_DISPLAY_CONFIG)
		return "display-config";
	if (partition_id == RMI_PARTITION_ID_EXTERNAL_TOUCH_AFE_CONFIG)
		return "external-touch-afe-config";
	if (partition_id == RMI_PARTITION_ID_UTILITY_PARAMETER)
		return "utility-parameter";
	return NULL;
}

gboolean
fu_synaptics_rmi_v7_device_detach (FuDevice *device, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	g_autoptr(GByteArray) enable_req = g_byte_array_new ();
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash (self);
	FuSynapticsRmiFunction *f34;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* disable interrupts */
	if (!fu_synaptics_rmi_device_disable_irqs (self, error))
		return FALSE;

	/* enter BL */
	fu_byte_array_append_uint8 (enable_req, RMI_PARTITION_ID_BOOTLOADER);
	fu_byte_array_append_uint32 (enable_req, 0x0, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8 (enable_req, RMI_FLASH_CMD_ENTER_BL);
	fu_byte_array_append_uint8 (enable_req, flash->bootloader_id[0]);
	fu_byte_array_append_uint8 (enable_req, flash->bootloader_id[1]);
	if (!fu_synaptics_rmi_device_write (self,
					    f34->data_base + 1,
					    enable_req,
					    error)) {
		g_prefix_error (error, "failed to enable programming: ");
		return FALSE;
	}

	/* wait for idle */
	if (!fu_synaptics_rmi_device_wait_for_idle (self, RMI_F34_ENABLE_WAIT_MS,
						    RMI_DEVICE_WAIT_FOR_IDLE_FLAG_NONE,
						    error))
		return FALSE;
	if (!fu_synaptics_rmi_device_poll_wait (self, error))
		return FALSE;
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	g_usleep (1000 * RMI_F34_ENABLE_WAIT_MS);
	return fu_synaptics_rmi_device_rebind_driver (self, error);
}

static gboolean
fu_synaptics_rmi_v7_device_erase_all (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFunction *f34;
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash (self);
	g_autoptr(GByteArray) erase_cmd = g_byte_array_new ();

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	fu_byte_array_append_uint8 (erase_cmd, RMI_PARTITION_ID_CORE_CODE);
	fu_byte_array_append_uint32 (erase_cmd, 0x0, G_LITTLE_ENDIAN);
	if (flash->bootloader_id[1] == 8) {
		/* For bootloader v8 */
		fu_byte_array_append_uint8 (erase_cmd, RMI_FLASH_CMD_ERASE_AP);
	} else {
		/* For bootloader v7 */
		fu_byte_array_append_uint8 (erase_cmd, RMI_FLASH_CMD_ERASE);
	}
	fu_byte_array_append_uint8 (erase_cmd, flash->bootloader_id[0]);
	fu_byte_array_append_uint8 (erase_cmd, flash->bootloader_id[1]);
	/* for BL8 device, we need hold 1 seconds after querying F34 status to
	 * avoid not get attention by following giving erase command */
	if (flash->bootloader_id[1] == 8)
		g_usleep (1000 * 1000);
	if (!fu_synaptics_rmi_device_write (self,
					    f34->data_base + 1,
					    erase_cmd,
					    error)) {
		g_prefix_error (error, "failed to unlock erasing: ");
		return FALSE;
	}
	g_usleep (1000 * 100);
	if (flash->bootloader_id[1] == 8){
		/* wait for ATTN */
		if (!fu_synaptics_rmi_device_wait_for_idle (self,
							    RMI_F34_ERASE_WAIT_MS,
							    RMI_DEVICE_WAIT_FOR_IDLE_FLAG_NONE,
							    error)) {
			g_prefix_error (error, "failed to wait for idle: ");
			return FALSE;
		}
	}
	if (!fu_synaptics_rmi_device_poll_wait (self, error)) {
		g_prefix_error (error, "failed to get flash success: ");
		return FALSE;
	}

	/* for BL7, we need erase config partition */
	if (flash->bootloader_id[1] == 7) {
		g_autoptr(GByteArray) erase_config_cmd = g_byte_array_new ();

		fu_byte_array_append_uint8 (erase_config_cmd, RMI_PARTITION_ID_CORE_CONFIG);
		fu_byte_array_append_uint32 (erase_config_cmd, 0x0, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint8 (erase_config_cmd, RMI_FLASH_CMD_ERASE);

		g_usleep (1000 * 100);
		if (!fu_synaptics_rmi_device_write (self,
						    f34->data_base + 1,
						    erase_config_cmd,
						    error)) {
			g_prefix_error (error, "failed to erase core config: ");
			return FALSE;
		}

		/* wait for ATTN */
		g_usleep (1000 * 100);
		if (!fu_synaptics_rmi_device_wait_for_idle (self,
							    RMI_F34_ERASE_WAIT_MS,
							    RMI_DEVICE_WAIT_FOR_IDLE_FLAG_REFRESH_F34,
							    error)) {
			g_prefix_error (error, "failed to wait for idle: ");
			return FALSE;
		}
		if (!fu_synaptics_rmi_device_poll_wait (self, error)) {
			g_prefix_error (error, "failed to get flash success: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_v7_device_write_blocks (FuSynapticsRmiDevice *self,
					 guint32 address,
					 const guint8 *data,
					 guint32 datasz,
					 GError **error)
{
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash (self);
	g_autoptr(GPtrArray) chunks = NULL;

	/* write FW blocks */
	chunks = fu_chunk_array_new (data, datasz,
				     0x00,	/* start addr */
				     0x00,	/* page_sz */
				     flash->block_size);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		g_autoptr(GByteArray) req = g_byte_array_new ();
		g_byte_array_append (req, chk->data, chk->data_sz);
		if (!fu_synaptics_rmi_device_write (self, address, req, error)) {
			g_prefix_error (error, "failed to write block @0x%x:%x ", address, chk->address);
			return FALSE;
		}
	}

	/* wait for idle */
	if (!fu_synaptics_rmi_device_wait_for_idle (self,
						    RMI_F34_IDLE_WAIT_MS,
						    RMI_DEVICE_WAIT_FOR_IDLE_FLAG_NONE,
						    error)) {
		g_prefix_error (error, "failed to wait for idle @0x%x: ", address);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_v7_device_write_partition (FuSynapticsRmiDevice *self,
					    RmiPartitionId partition_id,
					    GBytes *bytes,
					    GError **error)
{
	FuSynapticsRmiFunction *f34;
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash (self);
	g_autoptr(GByteArray) req_offset = g_byte_array_new ();
	g_autoptr(GByteArray) req_partition_id = g_byte_array_new ();
	g_autoptr(GPtrArray) chunks = NULL;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* write partition id */
	g_debug ("writing partition %s…",
		 rmi_firmware_partition_id_to_string (partition_id));
	fu_byte_array_append_uint8 (req_partition_id, partition_id);
	if (!fu_synaptics_rmi_device_write (self,
					    f34->data_base + 0x1,
					    req_partition_id,
					    error)) {
		g_prefix_error (error, "failed to write flash partition: ");
		return FALSE;
	}
	fu_byte_array_append_uint16 (req_offset, 0x0, G_LITTLE_ENDIAN);
	if (!fu_synaptics_rmi_device_write (self,
					    f34->data_base + 0x2,
					    req_offset,
					    error)) {
		g_prefix_error (error, "failed to write offset: ");
		return FALSE;
	}

	/* write partition */
	chunks = fu_chunk_array_new_from_bytes (bytes,
						0x00,	/* start addr */
						0x00,	/* page_sz */
						(gsize) flash->payload_length *
						(gsize) flash->block_size);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		g_autoptr(GByteArray) req_trans_sz = g_byte_array_new ();
		g_autoptr(GByteArray) req_cmd = g_byte_array_new ();
		fu_byte_array_append_uint16 (req_trans_sz,
					     chk->data_sz / flash->block_size,
					     G_LITTLE_ENDIAN);
		if (!fu_synaptics_rmi_device_write (self,
						    f34->data_base + 0x3,
						    req_trans_sz,
						    error)) {
			g_prefix_error (error, "failed to write transfer length: ");
			return FALSE;
		}
		fu_byte_array_append_uint8 (req_cmd, RMI_FLASH_CMD_WRITE);
		if (!fu_synaptics_rmi_device_write (self,
						    f34->data_base + 0x4,
						    req_cmd,
						    error)) {
			g_prefix_error (error, "failed to flash command: ");
			return FALSE;
		}
		if (!fu_synaptics_rmi_v7_device_write_blocks (self,
							      f34->data_base + 0x5,
							      chk->data,
							      chk->data_sz,
							      error))
			return FALSE;
		fu_device_set_progress_full (FU_DEVICE (self), (gsize) i, (gsize) chunks->len);
	}
	return TRUE;
}

gboolean
fu_synaptics_rmi_v7_device_write_firmware (FuDevice *device,
					   FuFirmware *firmware,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash (self);
	FuSynapticsRmiFunction *f34;
	g_autoptr(GBytes) bytes_bin = NULL;
	g_autoptr(GBytes) bytes_cfg = NULL;
	g_autoptr(GBytes) bytes_flashcfg = NULL;

	/* we should be in bootloader mode now, but check anyway */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not bootloader, perhaps need detach?!");
		return FALSE;
	}

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* get both images */
	bytes_bin = fu_firmware_get_image_by_id_bytes (firmware, "ui", error);
	if (bytes_bin == NULL)
		return FALSE;
	bytes_cfg = fu_firmware_get_image_by_id_bytes (firmware, "config", error);
	if (bytes_cfg == NULL)
		return FALSE;
	if (flash->bootloader_id[1] == 8) {
		bytes_flashcfg = fu_firmware_get_image_by_id_bytes (firmware, "flash-config", error);
		if (bytes_flashcfg == NULL)
			return FALSE;
	}

	/* disable powersaving */
	if (!fu_synaptics_rmi_device_disable_sleep (self, error))
		return FALSE;

	/* erase all */
	g_debug ("erasing…");
	if (!fu_synaptics_rmi_v7_device_erase_all (self, error)) {
		g_prefix_error (error, "failed to erase all: ");
		return FALSE;
	}

	/* write flash config for v8 */
	if (bytes_flashcfg != NULL) {
		if (!fu_synaptics_rmi_v7_device_write_partition (self,
								 RMI_PARTITION_ID_FLASH_CONFIG,
								 bytes_flashcfg,
								 error))
			return FALSE;
	}

	/* write core code */
	if (!fu_synaptics_rmi_v7_device_write_partition (self,
							 RMI_PARTITION_ID_CORE_CODE,
							 bytes_bin,
							 error))
		return FALSE;

	/* write core config */
	if (!fu_synaptics_rmi_v7_device_write_partition (self,
							 RMI_PARTITION_ID_CORE_CONFIG,
							 bytes_cfg,
							 error))
		return FALSE;

	/* success */
	return TRUE;
}

typedef struct __attribute__((packed)) {
	guint16			 partition_id;
	guint16			 partition_len;
	guint16			 partition_addr;
	guint16			 partition_prop;
} RmiPartitionTbl;

G_STATIC_ASSERT(sizeof(RmiPartitionTbl) == 8);

static gboolean
fu_synaptics_rmi_device_read_flash_config_v7 (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash (self);
	FuSynapticsRmiFunction *f34;
	g_autoptr(GByteArray) req_addr_zero = g_byte_array_new ();
	g_autoptr(GByteArray) req_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) req_partition_id = g_byte_array_new ();
	g_autoptr(GByteArray) req_transfer_length = g_byte_array_new ();
	g_autoptr(GByteArray) res = NULL;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* set partition id for bootloader 7 */
	fu_byte_array_append_uint8 (req_partition_id, RMI_PARTITION_ID_FLASH_CONFIG);
	if (!fu_synaptics_rmi_device_write (self,
					    f34->data_base + 0x1,
					    req_partition_id,
					    error)) {
		g_prefix_error (error, "failed to write flash partition id: ");
		return FALSE;
	}
	fu_byte_array_append_uint16 (req_addr_zero, 0x0, G_LITTLE_ENDIAN);
	if (!fu_synaptics_rmi_device_write (self,
					    f34->data_base + 0x2,
					    req_addr_zero,
					    error)) {
		g_prefix_error (error, "failed to write flash config address: ");
		return FALSE;
	}

	/* set transfer length */
	fu_byte_array_append_uint16 (req_transfer_length,
				     flash->config_length,
				     G_LITTLE_ENDIAN);
	if (!fu_synaptics_rmi_device_write (self,
					    f34->data_base + 0x3,
					    req_transfer_length,
					    error)) {
		g_prefix_error (error, "failed to set transfer length: ");
		return FALSE;
	}

	/* set command to read */
	fu_byte_array_append_uint8 (req_cmd, RMI_FLASH_CMD_READ);
	if (!fu_synaptics_rmi_device_write (self,
					    f34->data_base + 0x4,
					    req_cmd,
					    error)) {
		g_prefix_error (error, "failed to write command to read: ");
		return FALSE;
	}
	if (!fu_synaptics_rmi_device_poll_wait (self, error)) {
		g_prefix_error (error, "failed to wait: ");
		return FALSE;
	}

	/* read back entire buffer in blocks */
	res = fu_synaptics_rmi_device_read (self,
					    f34->data_base + 0x5,
					    (guint32) flash->block_size * (guint32) flash->config_length,
					    error);
	if (res == NULL) {
		g_prefix_error (error, "failed to read: ");
		return FALSE;
	}

	/* debugging */
	if (g_getenv ("FWUPD_SYNAPTICS_RMI_VERBOSE") != NULL) {
		fu_common_dump_full (G_LOG_DOMAIN, "FlashConfig", res->data, res->len,
				     80, FU_DUMP_FLAGS_NONE);
	}

	/* parse the config length */
	for (guint i = 0x2; i < res->len; i += sizeof(RmiPartitionTbl)) {
		RmiPartitionTbl tbl;
		if (!fu_memcpy_safe ((guint8 *) &tbl, sizeof(tbl), 0x0,			/* dst */
				     res->data, res->len, i,				/* src */
				     sizeof(tbl), error))
			return FALSE;
		g_debug ("found partition %s (0x%02x)",
			 rmi_firmware_partition_id_to_string (tbl.partition_id),
			 tbl.partition_id);
		if (tbl.partition_id == RMI_PARTITION_ID_CORE_CONFIG) {
			flash->block_count_cfg = tbl.partition_len;
			continue;
		}
		if (tbl.partition_id == RMI_PARTITION_ID_CORE_CODE) {
			flash->block_count_fw = tbl.partition_len;
			continue;
		}
		if (tbl.partition_id == RMI_PARTITION_ID_NONE)
			break;
	}

	/* success */
	return TRUE;
}

gboolean
fu_synaptics_rmi_v7_device_setup (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash (self);
	FuSynapticsRmiFunction *f34;
	guint8 offset;
	g_autoptr(GByteArray) f34_data0 = NULL;
	g_autoptr(GByteArray) f34_dataX = NULL;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	f34_data0 = fu_synaptics_rmi_device_read (self, f34->query_base, 1, error);
	if (f34_data0 == NULL) {
		g_prefix_error (error, "failed to read bootloader ID: ");
		return FALSE;
	}
	offset = (f34_data0->data[0] & 0b00000111) + 1;
	f34_dataX = fu_synaptics_rmi_device_read (self, f34->query_base + offset, 21, error);
	if (f34_dataX == NULL)
		return FALSE;
	flash->bootloader_id[0] = f34_dataX->data[0x0];
	flash->bootloader_id[1] = f34_dataX->data[0x1];
	flash->block_size = fu_common_read_uint16 (f34_dataX->data + 0x07, G_LITTLE_ENDIAN);
	flash->config_length = fu_common_read_uint16 (f34_dataX->data + 0x0d, G_LITTLE_ENDIAN);
	flash->payload_length = fu_common_read_uint16 (f34_dataX->data + 0x0f, G_LITTLE_ENDIAN);
	flash->build_id = fu_common_read_uint32 (f34_dataX->data + 0x02, G_LITTLE_ENDIAN);

	/* sanity check */
	if ((guint32) flash->block_size * (guint32) flash->config_length > G_MAXUINT16) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "block size 0x%x or config length 0x%x invalid",
			     flash->block_size, flash->config_length);
		return FALSE;
	}

	/* read flash config */
	return fu_synaptics_rmi_device_read_flash_config_v7 (self, error);
}

gboolean
fu_synaptics_rmi_v7_device_query_status (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFunction *f34;
	guint8 status;
	g_autoptr(GByteArray) f34_data = NULL;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (f34 == NULL)
		return FALSE;
	f34_data = fu_synaptics_rmi_device_read (self, f34->data_base, 0x1, error);
	if (f34_data == NULL) {
		g_prefix_error (error, "failed to read the f01 data base: ");
		return FALSE;
	}
	status = f34_data->data[0];
	if (status & 0x80) {
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}
	if (status == 0x01) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "operation only supported in bootloader mode");
		return FALSE;
	}
	if (status == 0x02) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "partition ID is not supported by the bootloader");
		return FALSE;
	}
	if (status == 0x03) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "partition supported, but command not supported");
		return FALSE;
	}
	if (status == 0x04) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid block offset");
		return FALSE;
	}
	if (status == 0x05) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid transfer");
		return FALSE;
	}
	if (status == 0x06) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "partition has not been erased");
		return FALSE;
	}
	if (status == 0x07) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_SIGNATURE_INVALID,
				     "flash programming key incorrect");
		return FALSE;
	}
	if (status == 0x08) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "bad partition table");
		return FALSE;
	}
	if (status == 0x09) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "transfer checksum failed");
		return FALSE;
	}
	if (status == 0x1f) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "flash hardware failure");
		return FALSE;
	}
	return TRUE;
}
