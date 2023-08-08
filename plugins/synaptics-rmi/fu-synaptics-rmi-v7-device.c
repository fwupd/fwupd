/*
 * Copyright (C) 2012 Andrew Duggan
 * Copyright (C) 2012 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-rmi-struct.h"
#include "fu-synaptics-rmi-v7-device.h"

#define RMI_F34_ERASE_WAIT_MS 10000 /* ms */

typedef enum {
	RMI_FLASH_CMD_IDLE = 0x00,
	RMI_FLASH_CMD_ENTER_BL,
	RMI_FLASH_CMD_READ,
	RMI_FLASH_CMD_WRITE,
	RMI_FLASH_CMD_ERASE,
	RMI_FLASH_CMD_ERASE_AP,
	RMI_FLASH_CMD_SENSOR_ID,
	RMI_FLASH_CMD_SIGNATURE,
} RmiFlashCommand;

gboolean
fu_synaptics_rmi_v7_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE(device);
	g_autoptr(GByteArray) enable_req = g_byte_array_new();
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	FuSynapticsRmiFunction *f34;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* disable interrupts */
	if (!fu_synaptics_rmi_device_disable_irqs(self, error))
		return FALSE;

	/* enter BL */
	fu_byte_array_append_uint8(enable_req, FU_RMI_PARTITION_ID_BOOTLOADER);
	fu_byte_array_append_uint32(enable_req, 0x0, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8(enable_req, RMI_FLASH_CMD_ENTER_BL);
	fu_byte_array_append_uint8(enable_req, flash->bootloader_id[0]);
	fu_byte_array_append_uint8(enable_req, flash->bootloader_id[1]);
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 1,
					   enable_req,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to enable programming: ");
		return FALSE;
	}

	/* wait for idle */
	if (!fu_synaptics_rmi_device_wait_for_idle(self,
						   RMI_F34_ENABLE_WAIT_MS,
						   RMI_DEVICE_WAIT_FOR_IDLE_FLAG_NONE,
						   error))
		return FALSE;
	if (!fu_synaptics_rmi_device_poll_wait(self, error))
		return FALSE;
	fu_device_sleep(device, RMI_F34_ENABLE_WAIT_MS);
	return TRUE;
}

static gboolean
fu_synaptics_rmi_v7_device_erase_partition(FuSynapticsRmiDevice *self,
					   guint8 partition_id,
					   GError **error)
{
	FuSynapticsRmiFunction *f34;
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	g_autoptr(GByteArray) erase_cmd = g_byte_array_new();

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;
	fu_byte_array_append_uint8(erase_cmd, partition_id);
	fu_byte_array_append_uint32(erase_cmd, 0x0, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8(erase_cmd, RMI_FLASH_CMD_ERASE);

	fu_byte_array_append_uint8(erase_cmd, flash->bootloader_id[0]);
	fu_byte_array_append_uint8(erase_cmd, flash->bootloader_id[1]);

	fu_device_sleep(FU_DEVICE(self), 1000); /* ms */
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 1,
					   erase_cmd,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to unlock erasing: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 100); /* ms */

	/* wait for ATTN */
	if (!fu_synaptics_rmi_device_wait_for_idle(self,
						   RMI_F34_ERASE_WAIT_MS,
						   RMI_DEVICE_WAIT_FOR_IDLE_FLAG_NONE,
						   error)) {
		g_prefix_error(error, "failed to wait for idle: ");
		return FALSE;
	}

	if (!fu_synaptics_rmi_device_poll_wait(self, error)) {
		g_prefix_error(error, "failed to get flash success: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_v7_device_erase_all(FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFunction *f34;
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	g_autoptr(GByteArray) erase_cmd = g_byte_array_new();

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	fu_byte_array_append_uint8(erase_cmd, FU_RMI_PARTITION_ID_CORE_CODE);
	fu_byte_array_append_uint32(erase_cmd, 0x0, G_LITTLE_ENDIAN);
	if (flash->bootloader_id[1] >= 8) {
		/* For bootloader v8 */
		fu_byte_array_append_uint8(erase_cmd, RMI_FLASH_CMD_ERASE_AP);
	} else {
		/* For bootloader v7 */
		fu_byte_array_append_uint8(erase_cmd, RMI_FLASH_CMD_ERASE);
	}
	fu_byte_array_append_uint8(erase_cmd, flash->bootloader_id[0]);
	fu_byte_array_append_uint8(erase_cmd, flash->bootloader_id[1]);
	/* for BL8 device, we need hold 1 seconds after querying F34 status to
	 * avoid not get attention by following giving erase command */
	if (flash->bootloader_id[1] >= 8)
		fu_device_sleep(FU_DEVICE(self), 1000); /* ms */
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 1,
					   erase_cmd,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to unlock erasing: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), 100); /* ms */
	if (flash->bootloader_id[1] >= 8) {
		/* wait for ATTN */
		if (!fu_synaptics_rmi_device_wait_for_idle(self,
							   RMI_F34_ERASE_WAIT_MS,
							   RMI_DEVICE_WAIT_FOR_IDLE_FLAG_NONE,
							   error)) {
			g_prefix_error(error, "failed to wait for idle: ");
			return FALSE;
		}
	}
	if (!fu_synaptics_rmi_device_poll_wait(self, error)) {
		g_prefix_error(error, "failed to get flash success: ");
		return FALSE;
	}

	/* for BL7, we need erase config partition */
	if (flash->bootloader_id[1] == 7) {
		g_autoptr(GByteArray) erase_config_cmd = g_byte_array_new();

		fu_byte_array_append_uint8(erase_config_cmd, FU_RMI_PARTITION_ID_CORE_CONFIG);
		fu_byte_array_append_uint32(erase_config_cmd, 0x0, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint8(erase_config_cmd, RMI_FLASH_CMD_ERASE);

		fu_device_sleep(FU_DEVICE(self), 100); /* ms */
		if (!fu_synaptics_rmi_device_write(self,
						   f34->data_base + 1,
						   erase_config_cmd,
						   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						   error)) {
			g_prefix_error(error, "failed to erase core config: ");
			return FALSE;
		}

		/* wait for ATTN */
		fu_device_sleep(FU_DEVICE(self), 100); /* ms */
		if (!fu_synaptics_rmi_device_wait_for_idle(
			self,
			RMI_F34_ERASE_WAIT_MS,
			RMI_DEVICE_WAIT_FOR_IDLE_FLAG_REFRESH_F34,
			error)) {
			g_prefix_error(error, "failed to wait for idle: ");
			return FALSE;
		}
		if (!fu_synaptics_rmi_device_poll_wait(self, error)) {
			g_prefix_error(error, "failed to get flash success: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_v7_device_write_blocks(FuSynapticsRmiDevice *self,
					guint32 address,
					const guint8 *data,
					guint32 datasz,
					GError **error)
{
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	g_autoptr(GPtrArray) chunks = NULL;

	/* write FW blocks */
	chunks = fu_chunk_array_new(data,
				    datasz,
				    0x00, /* start addr */
				    0x00, /* page_sz */
				    flash->block_size);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) req = g_byte_array_new();
		g_byte_array_append(req, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		if (!fu_synaptics_rmi_device_write(self,
						   address,
						   req,
						   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						   error)) {
			g_prefix_error(error,
				       "failed to write block @0x%x:%x: ",
				       address,
				       fu_chunk_get_address(chk));
			return FALSE;
		}
	}

	/* wait for idle */
	if (!fu_synaptics_rmi_device_wait_for_idle(self,
						   RMI_F34_IDLE_WAIT_MS,
						   RMI_DEVICE_WAIT_FOR_IDLE_FLAG_NONE,
						   error)) {
		g_prefix_error(error, "failed to wait for idle @0x%x: ", address);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_v7_device_write_partition_signature(FuSynapticsRmiDevice *self,
						     FuFirmware *firmware,
						     const gchar *id,
						     FuRmiPartitionId partition_id,
						     GError **error)
{
	FuSynapticsRmiFunction *f34;
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	g_autoptr(GByteArray) req_offset = g_byte_array_new();
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GBytes) bytes = NULL;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/*check if signature exists */
	bytes =
	    fu_firmware_get_image_by_id_bytes(firmware, g_strdup_printf("%s-signature", id), NULL);
	if (bytes == NULL) {
		return TRUE;
	}

	/* write partition signature */
	g_info("writing partition signature %s…", fu_rmi_partition_id_to_string(partition_id));

	fu_byte_array_append_uint16(req_offset, 0x0, G_LITTLE_ENDIAN);
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 0x2,
					   req_offset,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to write offset: ");
		return FALSE;
	}

	chunks =
	    fu_chunk_array_new_from_bytes(bytes,
					  0x00, /* start addr */
					  0x00, /* page_sz */
					  (gsize)flash->payload_length * (gsize)flash->block_size);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) req_trans_sz = g_byte_array_new();
		g_autoptr(GByteArray) req_cmd = g_byte_array_new();
		fu_byte_array_append_uint16(req_trans_sz,
					    fu_chunk_get_data_sz(chk) / flash->block_size,
					    G_LITTLE_ENDIAN);
		if (!fu_synaptics_rmi_device_write(self,
						   f34->data_base + 0x3,
						   req_trans_sz,
						   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						   error)) {
			g_prefix_error(error, "failed to write transfer length: ");
			return FALSE;
		}
		fu_byte_array_append_uint8(req_cmd, RMI_FLASH_CMD_SIGNATURE);
		if (!fu_synaptics_rmi_device_write(self,
						   f34->data_base + 0x4,
						   req_cmd,
						   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						   error)) {
			g_prefix_error(error, "failed to write signature command: ");
			return FALSE;
		}
		if (!fu_synaptics_rmi_v7_device_write_blocks(self,
							     f34->data_base + 0x5,
							     fu_chunk_get_data(chk),
							     fu_chunk_get_data_sz(chk),
							     error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_v7_device_write_partition(FuSynapticsRmiDevice *self,
					   FuFirmware *firmware,
					   const gchar *id,
					   FuRmiPartitionId partition_id,
					   GBytes *bytes,
					   FuProgress *progress,
					   GError **error)
{
	FuSynapticsRmiFunction *f34;
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	g_autoptr(GByteArray) req_offset = g_byte_array_new();
	g_autoptr(GByteArray) req_partition_id = g_byte_array_new();
	g_autoptr(GPtrArray) chunks = NULL;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* write partition id */
	g_info("writing partition %s…", fu_rmi_partition_id_to_string(partition_id));
	fu_byte_array_append_uint8(req_partition_id, partition_id);
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 0x1,
					   req_partition_id,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to write flash partition: ");
		return FALSE;
	}
	fu_byte_array_append_uint16(req_offset, 0x0, G_LITTLE_ENDIAN);
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 0x2,
					   req_offset,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to write offset: ");
		return FALSE;
	}

	/* write partition */
	chunks =
	    fu_chunk_array_new_from_bytes(bytes,
					  0x00, /* start addr */
					  0x00, /* page_sz */
					  (gsize)flash->payload_length * (gsize)flash->block_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len + 1);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) req_trans_sz = g_byte_array_new();
		g_autoptr(GByteArray) req_cmd = g_byte_array_new();
		fu_byte_array_append_uint16(req_trans_sz,
					    fu_chunk_get_data_sz(chk) / flash->block_size,
					    G_LITTLE_ENDIAN);
		if (!fu_synaptics_rmi_device_write(self,
						   f34->data_base + 0x3,
						   req_trans_sz,
						   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						   error)) {
			g_prefix_error(error, "failed to write transfer length: ");
			return FALSE;
		}
		fu_byte_array_append_uint8(req_cmd, RMI_FLASH_CMD_WRITE);
		if (!fu_synaptics_rmi_device_write(self,
						   f34->data_base + 0x4,
						   req_cmd,
						   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						   error)) {
			g_prefix_error(error, "failed to flash command: ");
			return FALSE;
		}
		if (!fu_synaptics_rmi_v7_device_write_blocks(self,
							     f34->data_base + 0x5,
							     fu_chunk_get_data(chk),
							     fu_chunk_get_data_sz(chk),
							     error))
			return FALSE;
		fu_progress_step_done(progress);
	}
	if (!fu_synaptics_rmi_v7_device_write_partition_signature(self,
								  firmware,
								  id,
								  partition_id,
								  error))
		return FALSE;
	fu_progress_step_done(progress);
	return TRUE;
}

static GBytes *
fu_synaptics_rmi_v7_device_get_pubkey(FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	FuSynapticsRmiFunction *f34;
	const gsize key_size = RMI_KEY_SIZE_2K;
	g_autoptr(GByteArray) req_addr_zero = g_byte_array_new();
	g_autoptr(GByteArray) req_cmd = g_byte_array_new();
	g_autoptr(GByteArray) req_partition_id = g_byte_array_new();
	g_autoptr(GByteArray) req_transfer_length = g_byte_array_new();
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(GByteArray) pubkey = g_byte_array_new();

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return NULL;

	/* set partition id for bootloader 7 */
	fu_byte_array_append_uint8(req_partition_id, FU_RMI_PARTITION_ID_PUBKEY);
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 0x1,
					   req_partition_id,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to write flash partition id: ");
		return NULL;
	}
	fu_byte_array_append_uint16(req_addr_zero, 0x0, G_LITTLE_ENDIAN);
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 0x2,
					   req_addr_zero,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to write flash config address: ");
		return NULL;
	}

	/* set transfer length */
	fu_byte_array_append_uint16(req_transfer_length,
				    key_size / flash->block_size,
				    G_LITTLE_ENDIAN);
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 0x3,
					   req_transfer_length,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to set transfer length: ");
		return NULL;
	}

	/* set command to read */
	fu_byte_array_append_uint8(req_cmd, RMI_FLASH_CMD_READ);
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 0x4,
					   req_cmd,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to write command to read: ");
		return NULL;
	}
	if (!fu_synaptics_rmi_device_poll_wait(self, error)) {
		g_prefix_error(error, "failed to wait: ");
		return NULL;
	}

	/* read back entire buffer in blocks */
	res = fu_synaptics_rmi_device_read(self, f34->data_base + 0x5, (guint32)key_size, error);
	if (res == NULL) {
		g_prefix_error(error, "failed to read: ");
		return NULL;
	}

	for (guint i = 0; i < res->len; i++)
		fu_byte_array_append_uint8(pubkey, res->data[res->len - i - 1]);

	/* success */
	return g_bytes_new(pubkey->data, pubkey->len);
}

static gboolean
fu_synaptics_rmi_v7_device_secure_check(FuSynapticsRmiDevice *self,
					FuFirmware *firmware,
					GError **error)
{
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	g_autoptr(GBytes) pubkey = NULL;
	g_autoptr(GPtrArray) imgs = NULL;

	if (flash->bootloader_id[1] >= 10 || flash->has_pubkey == FALSE)
		return TRUE;

	pubkey = fu_synaptics_rmi_v7_device_get_pubkey(self, error);
	if (pubkey == NULL) {
		g_prefix_error(error, "get pubkey failed: ");
		return FALSE;
	}

	imgs = fu_firmware_get_images(firmware);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		const gchar *id = fu_firmware_get_id(img);
		g_autoptr(GBytes) byte_payload = NULL;
		g_autoptr(GBytes) byte_signature = NULL;
		g_autofree gchar *id_signature = NULL;

		if (g_str_has_suffix(id, "-signature"))
			continue;
		id_signature = g_strdup_printf("%s-signature", id);
		byte_signature = fu_firmware_get_image_by_id_bytes(firmware, id_signature, NULL);
		if (byte_signature == NULL)
			continue;
		byte_payload = fu_firmware_get_bytes(img, error);
		if (byte_payload == NULL)
			return FALSE;
		if (!fu_synaptics_verify_sha256_signature(byte_payload,
							  pubkey,
							  byte_signature,
							  error)) {
			g_prefix_error(error, "%s secure check failed: ", id);
			return FALSE;
		}
		g_info("%s signature verified successfully", id);
	}
	return TRUE;
}

gboolean
fu_synaptics_rmi_v7_device_write_firmware(FuDevice *device,
					  FuFirmware *firmware,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE(device);
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	FuSynapticsRmiFunction *f34;
	g_autoptr(GBytes) bytes_bin = NULL;
	g_autoptr(GBytes) bytes_cfg = NULL;
	g_autoptr(GBytes) bytes_flashcfg = NULL;
	g_autoptr(GBytes) bytes_fld = NULL;
	g_autoptr(GBytes) bytes_afe = NULL;
	g_autoptr(GBytes) bytes_displayconfig = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	if (flash->bootloader_id[1] > 8) {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "disable-sleep");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 0, "verify-signature");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "fixed-location-data");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 8, "flash-config");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 9, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 81, "core-code");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "core-config");
		fu_progress_add_step(progress,
				     FWUPD_STATUS_DEVICE_WRITE,
				     0,
				     "external-touch-afe-config");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 0, "display-config");
	} else if (flash->bootloader_id[1] == 8) {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "disable-sleep");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 0, "verify-signature");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 0, "fixed-location-data");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 16, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 0, "flash-config");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 81, "core-code");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "core-config");
		fu_progress_add_step(progress,
				     FWUPD_STATUS_DEVICE_WRITE,
				     0,
				     "external-touch-afe-config");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 0, "display-config");
	} else {
		fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "disable-sleep");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 2, "verify-signature");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 2, "fixed-location-data");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 3, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 89, "core-code");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 2, "core-config");
		fu_progress_add_step(progress,
				     FWUPD_STATUS_DEVICE_WRITE,
				     2,
				     "external-touch-afe-config");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 2, "display-config");
	}

	/* we should be in bootloader mode now, but check anyway */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not bootloader, perhaps need detach?!");
		return FALSE;
	}

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* get both images */
	bytes_bin = fu_firmware_get_image_by_id_bytes(firmware, "ui", error);
	if (bytes_bin == NULL)
		return FALSE;
	bytes_cfg = fu_firmware_get_image_by_id_bytes(firmware, "config", error);
	if (bytes_cfg == NULL)
		return FALSE;
	if (flash->bootloader_id[1] >= 8) {
		bytes_flashcfg = fu_firmware_get_image_by_id_bytes(firmware, "flash-config", error);
		if (bytes_flashcfg == NULL)
			return FALSE;
	}
	bytes_fld = fu_firmware_get_image_by_id_bytes(firmware, "fixed-location-data", NULL);
	bytes_afe = fu_firmware_get_image_by_id_bytes(firmware, "afe-config", NULL);
	bytes_displayconfig = fu_firmware_get_image_by_id_bytes(firmware, "display-config", NULL);

	/* disable powersaving */
	if (!fu_synaptics_rmi_device_disable_sleep(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify signature */
	if (!fu_synaptics_rmi_v7_device_secure_check(self, firmware, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write fld before erase if exists */
	if (bytes_fld != NULL) {
		if (!fu_synaptics_rmi_v7_device_write_partition(
			self,
			firmware,
			"fixed-location-data",
			FU_RMI_PARTITION_ID_FIXED_LOCATION_DATA,
			bytes_fld,
			fu_progress_get_child(progress),
			error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* write flash config for BL > v8 */
	if (flash->bootloader_id[1] > 8) {
		if (!fu_synaptics_rmi_v7_device_erase_partition(self,
								FU_RMI_PARTITION_ID_FLASH_CONFIG,
								error))
			return FALSE;
		if (!fu_synaptics_rmi_v7_device_write_partition(self,
								firmware,
								"flash-config",
								FU_RMI_PARTITION_ID_FLASH_CONFIG,
								bytes_flashcfg,
								fu_progress_get_child(progress),
								error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* erase all */
	if (!fu_synaptics_rmi_v7_device_erase_all(self, error)) {
		g_prefix_error(error, "failed to erase all: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write flash config for v8 */
	if (flash->bootloader_id[1] == 8) {
		if (!fu_synaptics_rmi_v7_device_write_partition(self,
								firmware,
								"flash-config",
								FU_RMI_PARTITION_ID_FLASH_CONFIG,
								bytes_flashcfg,
								fu_progress_get_child(progress),
								error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* write core code */
	if (!fu_synaptics_rmi_v7_device_write_partition(self,
							firmware,
							"ui",
							FU_RMI_PARTITION_ID_CORE_CODE,
							bytes_bin,
							fu_progress_get_child(progress),
							error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write core config */
	if (!fu_synaptics_rmi_v7_device_write_partition(self,
							firmware,
							"config",
							FU_RMI_PARTITION_ID_CORE_CONFIG,
							bytes_cfg,
							fu_progress_get_child(progress),
							error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write afe-config if exists */
	if (bytes_afe != NULL) {
		if (!fu_synaptics_rmi_v7_device_write_partition(
			self,
			firmware,
			"afe-config",
			FU_RMI_PARTITION_ID_EXTERNAL_TOUCH_AFE_CONFIG,
			bytes_afe,
			fu_progress_get_child(progress),
			error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* write display config if exists */
	if (bytes_displayconfig != NULL) {
		if (!fu_synaptics_rmi_v7_device_write_partition(self,
								firmware,
								"display-config",
								FU_RMI_PARTITION_ID_DISPLAY_CONFIG,
								bytes_displayconfig,
								fu_progress_get_child(progress),
								error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_synaptics_rmi_device_read_flash_config_v7(FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	FuSynapticsRmiFunction *f34;
	g_autoptr(GByteArray) req_addr_zero = g_byte_array_new();
	g_autoptr(GByteArray) req_cmd = g_byte_array_new();
	g_autoptr(GByteArray) req_partition_id = g_byte_array_new();
	g_autoptr(GByteArray) req_transfer_length = g_byte_array_new();
	g_autoptr(GByteArray) res = NULL;
	gsize partition_size = FU_STRUCT_RMI_PARTITION_TBL_SIZE;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* set partition id for bootloader 7 */
	fu_byte_array_append_uint8(req_partition_id, FU_RMI_PARTITION_ID_FLASH_CONFIG);
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 0x1,
					   req_partition_id,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to write flash partition id: ");
		return FALSE;
	}
	fu_byte_array_append_uint16(req_addr_zero, 0x0, G_LITTLE_ENDIAN);
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 0x2,
					   req_addr_zero,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to write flash config address: ");
		return FALSE;
	}

	/* set transfer length */
	fu_byte_array_append_uint16(req_transfer_length, flash->config_length, G_LITTLE_ENDIAN);
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 0x3,
					   req_transfer_length,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to set transfer length: ");
		return FALSE;
	}

	/* set command to read */
	fu_byte_array_append_uint8(req_cmd, RMI_FLASH_CMD_READ);
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base + 0x4,
					   req_cmd,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to write command to read: ");
		return FALSE;
	}
	if (!fu_synaptics_rmi_device_poll_wait(self, error)) {
		g_prefix_error(error, "failed to wait: ");
		return FALSE;
	}

	/* read back entire buffer in blocks */
	res =
	    fu_synaptics_rmi_device_read(self,
					 f34->data_base + 0x5,
					 (guint32)flash->block_size * (guint32)flash->config_length,
					 error);
	if (res == NULL) {
		g_prefix_error(error, "failed to read: ");
		return FALSE;
	}

	/* debugging */
	fu_dump_full(G_LOG_DOMAIN, "FlashConfig", res->data, res->len, 80, FU_DUMP_FLAGS_NONE);

	if ((res->data[0] & 0x0f) == 1)
		partition_size += 0x2;

	/* parse the config length */
	for (guint i = 0x2; i < res->len; i += partition_size) {
		guint16 partition_id;
		g_autoptr(GByteArray) st_prt = NULL;
		st_prt = fu_struct_rmi_partition_tbl_parse(res->data, res->len, i, error);
		if (st_prt == NULL)
			return FALSE;
		partition_id = fu_struct_rmi_partition_tbl_get_partition_id(st_prt);
		g_debug("found partition %s (0x%02x)",
			fu_rmi_partition_id_to_string(partition_id),
			partition_id);
		if (partition_id == FU_RMI_PARTITION_ID_CORE_CONFIG) {
			flash->block_count_cfg =
			    fu_struct_rmi_partition_tbl_get_partition_len(st_prt);
			continue;
		}
		if (partition_id == FU_RMI_PARTITION_ID_CORE_CODE) {
			flash->block_count_fw =
			    fu_struct_rmi_partition_tbl_get_partition_len(st_prt);
			continue;
		}
		if (partition_id == FU_RMI_PARTITION_ID_PUBKEY) {
			flash->has_pubkey = TRUE;
			continue;
		}
		if (partition_id == FU_RMI_PARTITION_ID_NONE)
			break;
	}

	/* success */
	return TRUE;
}

gboolean
fu_synaptics_rmi_v7_device_setup(FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	FuSynapticsRmiFunction *f34;
	guint8 offset;
	g_autoptr(GByteArray) f34_data0 = NULL;
	g_autoptr(GByteArray) f34_dataX = NULL;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	f34_data0 = fu_synaptics_rmi_device_read(self, f34->query_base, 1, error);
	if (f34_data0 == NULL) {
		g_prefix_error(error, "failed to read bootloader ID: ");
		return FALSE;
	}
	offset = (f34_data0->data[0] & 0b00000111) + 1;
	f34_dataX = fu_synaptics_rmi_device_read(self, f34->query_base + offset, 21, error);
	if (f34_dataX == NULL)
		return FALSE;
	if (!fu_memread_uint8_safe(f34_dataX->data,
				   f34_dataX->len,
				   0x0,
				   &flash->bootloader_id[0],
				   error))
		return FALSE;
	if (!fu_memread_uint8_safe(f34_dataX->data,
				   f34_dataX->len,
				   0x1,
				   &flash->bootloader_id[1],
				   error))
		return FALSE;
	if (!fu_memread_uint16_safe(f34_dataX->data,
				    f34_dataX->len,
				    0x07,
				    &flash->block_size,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(f34_dataX->data,
				    f34_dataX->len,
				    0x0d,
				    &flash->config_length,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(f34_dataX->data,
				    f34_dataX->len,
				    0x0f,
				    &flash->payload_length,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint32_safe(f34_dataX->data,
				    f34_dataX->len,
				    0x02,
				    &flash->build_id,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	/* sanity check */
	if ((guint32)flash->block_size * (guint32)flash->config_length > G_MAXUINT16) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "block size 0x%x or config length 0x%x invalid",
			    flash->block_size,
			    flash->config_length);
		return FALSE;
	}

	/* read flash config */
	return fu_synaptics_rmi_device_read_flash_config_v7(self, error);
}

gboolean
fu_synaptics_rmi_v7_device_query_status(FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFunction *f34;
	guint8 status;
	g_autoptr(GByteArray) f34_data = NULL;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;
	f34_data = fu_synaptics_rmi_device_read(self, f34->data_base, 0x1, error);
	if (f34_data == NULL) {
		g_prefix_error(error, "failed to read the f01 data base: ");
		return FALSE;
	}
	status = f34_data->data[0];
	if (status & 0x80) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}
	if (status == 0x01) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "operation only supported in bootloader mode");
		return FALSE;
	}
	if (status == 0x02) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "partition ID is not supported by the bootloader");
		return FALSE;
	}
	if (status == 0x03) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "partition supported, but command not supported");
		return FALSE;
	}
	if (status == 0x04) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid block offset");
		return FALSE;
	}
	if (status == 0x05) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid transfer");
		return FALSE;
	}
	if (status == 0x06) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "partition has not been erased");
		return FALSE;
	}
	if (status == 0x07) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_SIGNATURE_INVALID,
				    "flash programming key incorrect");
		return FALSE;
	}
	if (status == 0x08) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "bad partition table");
		return FALSE;
	}
	if (status == 0x09) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "transfer checksum failed");
		return FALSE;
	}
	if (status == 0x1f) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "flash hardware failure");
		return FALSE;
	}
	return TRUE;
}
