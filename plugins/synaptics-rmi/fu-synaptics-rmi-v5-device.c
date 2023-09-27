/*
 * Copyright (C) 2012 Andrew Duggan
 * Copyright (C) 2012 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-rmi-firmware.h"
#include "fu-synaptics-rmi-v5-device.h"

#define RMI_F34_WRITE_FW_BLOCK	     0x02
#define RMI_F34_ERASE_ALL	     0x03
#define RMI_F34_WRITE_LOCKDOWN_BLOCK 0x04
#define RMI_F34_WRITE_CONFIG_BLOCK   0x06
#define RMI_F34_WRITE_SIGNATURE	     0x0b
#define RMI_F34_ENABLE_FLASH_PROG    0x0f

#define RMI_F34_BLOCK_SIZE_OFFSET    1
#define RMI_F34_FW_BLOCKS_OFFSET     3
#define RMI_F34_CONFIG_BLOCKS_OFFSET 5

#define RMI_F34_ERASE_WAIT_MS (5 * 1000) /* ms */

gboolean
fu_synaptics_rmi_v5_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE(device);
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	g_autoptr(GByteArray) enable_req = g_byte_array_new();

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}

	/* disable interrupts */
	if (!fu_synaptics_rmi_device_disable_irqs(self, error))
		return FALSE;
	if (!fu_synaptics_rmi_device_write_bus_select(self, 0, error)) {
		g_prefix_error(error, "failed to write bus select: ");
		return FALSE;
	}

	/* unlock bootloader and rebind kernel driver */
	if (!fu_synaptics_rmi_device_write_bootloader_id(self, error))
		return FALSE;
	fu_byte_array_append_uint8(enable_req, RMI_F34_ENABLE_FLASH_PROG);
	if (!fu_synaptics_rmi_device_write(self,
					   flash->status_addr,
					   enable_req,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to enable programming: ");
		return FALSE;
	}

	fu_device_sleep(device, RMI_F34_ENABLE_WAIT_MS);
	return TRUE;
}

static gboolean
fu_synaptics_rmi_v5_device_erase_all(FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFunction *f34;
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	g_autoptr(GByteArray) erase_cmd = g_byte_array_new();

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* all other versions */
	fu_byte_array_append_uint8(erase_cmd, RMI_F34_ERASE_ALL);
	if (!fu_synaptics_rmi_device_write(self,
					   flash->status_addr,
					   erase_cmd,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_ALLOW_FAILURE,
					   error)) {
		g_prefix_error(error, "failed to erase core config: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), RMI_F34_ERASE_WAIT_MS);
	fu_synaptics_rmi_device_set_iepmode(self, FALSE);
	if (!fu_synaptics_rmi_device_enter_iep_mode(self,
						    FU_SYNAPTICS_RMI_DEVICE_FLAG_FORCE,
						    error))
		return FALSE;
	if (!fu_synaptics_rmi_device_wait_for_idle(self,
						   RMI_F34_ERASE_WAIT_MS,
						   RMI_DEVICE_WAIT_FOR_IDLE_FLAG_REFRESH_F34,
						   error)) {
		g_prefix_error(error, "failed to wait for idle for erase: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_v5_device_write_block(FuSynapticsRmiDevice *self,
				       guint8 cmd,
				       guint32 address,
				       const guint8 *data,
				       gsize datasz,
				       GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new();

	g_byte_array_append(req, data, datasz);
	fu_byte_array_append_uint8(req, cmd);
	if (!fu_synaptics_rmi_device_write(self,
					   address,
					   req,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_ALLOW_FAILURE,
					   error)) {
		g_prefix_error(error, "failed to write block @0x%x: ", address);
		return FALSE;
	}
	if (!fu_synaptics_rmi_device_wait_for_idle(self,
						   RMI_F34_IDLE_WAIT_MS,
						   RMI_DEVICE_WAIT_FOR_IDLE_FLAG_NONE,
						   error)) {
		g_prefix_error(error, "failed to wait for idle @0x%x: ", address);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_v5_device_secure_check(FuDevice *device,
					GBytes *payload,
					GBytes *signature,
					GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE(device);
	FuSynapticsRmiFunction *f34;
	guint16 rsa_pubkey_len = fu_synaptics_rmi_device_get_sig_size(self) / 8;
	guint16 rsa_block_cnt = rsa_pubkey_len / 3;
	guint16 rsa_block_remain = rsa_pubkey_len % 3;
	g_autoptr(GByteArray) pubkey_buf = g_byte_array_new();
	g_autoptr(GBytes) pubkey = NULL;

	fu_dump_bytes(G_LOG_DOMAIN, "Signature", signature);

	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* parse RSA public key modulus */
	if (rsa_block_remain > 0)
		rsa_block_cnt += 1;
	for (guint retries = 0;; retries++) {
		/* need read another register to reset the offset of packet register */
		if (!fu_synaptics_rmi_v5_device_query_status(self, error)) {
			g_prefix_error(error, "failed to read status: ");
			return FALSE;
		}
		if (!fu_synaptics_rmi_device_enter_iep_mode(self,
							    FU_SYNAPTICS_RMI_DEVICE_FLAG_FORCE,
							    error))
			return FALSE;
		for (guint16 block_num = 0; block_num < rsa_block_cnt; block_num++) {
			g_autoptr(GByteArray) res = NULL;
			res = fu_synaptics_rmi_device_read_packet_register(
			    self,
			    f34->query_base + 14, /* addr of flash properties + 5 */
			    0x3,
			    error);
			if (res == NULL)
				return FALSE;
			if (res->len != 0x3)
				g_debug("read %u bytes in return", res->len);
			if (rsa_block_remain && block_num + 1 == rsa_block_cnt) {
				g_byte_array_remove_range(res,
							  rsa_block_remain,
							  res->len - rsa_block_remain);
			}
			for (guint i = 0; i < res->len / 2; i++) {
				guint8 tmp = res->data[i];
				res->data[i] = res->data[res->len - i - 1];
				res->data[res->len - i - 1] = tmp;
			}
			if (rsa_block_remain && block_num + 1 == rsa_block_cnt) {
				g_byte_array_prepend(pubkey_buf, res->data, rsa_block_remain);
			} else {
				g_byte_array_prepend(pubkey_buf, res->data, res->len);
			}
		}
		if (rsa_pubkey_len != pubkey_buf->len) {
			if (retries++ > 2) {
				g_set_error(
				    error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "RSA public key length not matched %u: after %u retries: ",
				    pubkey_buf->len,
				    retries);
				return FALSE;
			}
			g_byte_array_set_size(pubkey_buf, 0);
			continue;
		}

		/* success */
		break;
	}
	fu_dump_full(G_LOG_DOMAIN,
		     "RSA public key",
		     pubkey_buf->data,
		     pubkey_buf->len,
		     16,
		     FU_DUMP_FLAGS_NONE);

	/* sanity check size */
	if (rsa_pubkey_len != pubkey_buf->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "RSA public key length did not match: %u != %u: ",
			    rsa_pubkey_len,
			    pubkey_buf->len);
		return FALSE;
	}
	pubkey = g_bytes_new(pubkey_buf->data, pubkey_buf->len);
	return fu_synaptics_verify_sha256_signature(payload, pubkey, signature, error);
}

gboolean
fu_synaptics_rmi_v5_device_write_firmware(FuDevice *device,
					  FuFirmware *firmware,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE(device);
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	FuSynapticsRmiFunction *f34;
	FuSynapticsRmiFirmware *rmi_firmware = FU_SYNAPTICS_RMI_FIRMWARE(firmware);
	guint32 address;
	guint32 firmware_length =
	    fu_firmware_get_size(firmware) - fu_synaptics_rmi_firmware_get_sig_size(rmi_firmware);
	g_autoptr(GBytes) bytes_bin = NULL;
	g_autoptr(GBytes) bytes_cfg = NULL;
	g_autoptr(GBytes) signature_bin = NULL;
	g_autoptr(GBytes) firmware_bin = NULL;
	g_autoptr(FuChunkArray) chunks_bin = NULL;
	g_autoptr(FuChunkArray) chunks_cfg = NULL;
	g_autoptr(GByteArray) req_addr = g_byte_array_new();

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, "enter-iep");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 10, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "write-signature");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, "cfg-image");

	/* we should be in bootloader mode now, but check anyway */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not bootloader, perhaps need detach?!");
		return FALSE;
	}
	if (!fu_synaptics_rmi_device_enter_iep_mode(self,
						    FU_SYNAPTICS_RMI_DEVICE_FLAG_FORCE,
						    error))
		return FALSE;

	/* check is idle */
	if (!fu_synaptics_rmi_device_wait_for_idle(self,
						   0,
						   RMI_DEVICE_WAIT_FOR_IDLE_FLAG_REFRESH_F34,
						   error)) {
		g_prefix_error(error, "not idle: ");
		return FALSE;
	}
	if (fu_synaptics_rmi_firmware_get_sig_size(rmi_firmware) == 0 &&
	    fu_synaptics_rmi_device_get_sig_size(self) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "device secure but firmware not secure");
		return FALSE;
	}
	if (fu_synaptics_rmi_firmware_get_sig_size(rmi_firmware) != 0 &&
	    fu_synaptics_rmi_device_get_sig_size(self) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "device not secure but firmware secure");
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

	/* verify signature if set */
	firmware_bin = fu_bytes_new_offset(bytes_bin, 0, firmware_length, error);
	if (firmware_bin == NULL)
		return FALSE;
	signature_bin = fu_firmware_get_image_by_id_bytes(firmware, "sig", NULL);
	if (signature_bin != NULL) {
		if (!fu_synaptics_rmi_v5_device_secure_check(device,
							     firmware_bin,
							     signature_bin,
							     error)) {
			g_prefix_error(error, "secure check failed: ");
			return FALSE;
		}
	}

	/* disable powersaving */
	if (!fu_synaptics_rmi_device_disable_sleep(self, error)) {
		g_prefix_error(error, "failed to disable sleep: ");
		return FALSE;
	}

	/* unlock again */
	if (!fu_synaptics_rmi_device_write_bootloader_id(self, error)) {
		g_prefix_error(error, "failed to unlock again: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* erase all */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_synaptics_rmi_v5_device_erase_all(self, error)) {
		g_prefix_error(error, "failed to erase all: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write initial address */
	fu_byte_array_append_uint16(req_addr, 0x0, G_LITTLE_ENDIAN);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base,
					   req_addr,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to write 1st address zero: ");
		return FALSE;
	}

	/* write each block */
	if (f34->function_version == 0x01)
		address = f34->data_base + RMI_F34_BLOCK_DATA_V1_OFFSET;
	else
		address = f34->data_base + RMI_F34_BLOCK_DATA_OFFSET;
	chunks_bin = fu_chunk_array_new_from_bytes(firmware_bin, 0x00, flash->block_size);
	chunks_cfg = fu_chunk_array_new_from_bytes(bytes_cfg, 0x00, flash->block_size);
	for (guint i = 0; i < fu_chunk_array_length(chunks_bin); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks_bin, i);
		if (!fu_synaptics_rmi_v5_device_write_block(self,
							    RMI_F34_WRITE_FW_BLOCK,
							    address,
							    fu_chunk_get_data(chk),
							    fu_chunk_get_data_sz(chk),
							    error)) {
			g_prefix_error(error,
				       "failed to write bin block %u: ",
				       fu_chunk_get_idx(chk));
			return FALSE;
		}
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks_bin));
	}
	fu_progress_step_done(progress);

	/* payload signature */
	if (signature_bin != NULL && fu_synaptics_rmi_device_get_sig_size(self) != 0) {
		FuProgress *progress_child = fu_progress_get_child(progress);
		g_autoptr(FuChunkArray) chunks_sig = NULL;
		chunks_sig = fu_chunk_array_new_from_bytes(signature_bin,
							   0x00, /* start addr */
							   flash->block_size);
		if (!fu_synaptics_rmi_device_write(self,
						   f34->data_base,
						   req_addr,
						   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
						   error)) {
			g_prefix_error(error, "failed to write 1st address zero: ");
			return FALSE;
		}
		fu_progress_set_id(progress_child, G_STRLOC);
		fu_progress_set_steps(progress_child, fu_chunk_array_length(chunks_sig));
		for (guint i = 0; i < fu_chunk_array_length(chunks_sig); i++) {
			g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks_sig, i);
			if (!fu_synaptics_rmi_v5_device_write_block(self,
								    RMI_F34_WRITE_SIGNATURE,
								    address,
								    fu_chunk_get_data(chk),
								    fu_chunk_get_data_sz(chk),
								    error)) {
				g_prefix_error(error,
					       "failed to write bin block %u: ",
					       fu_chunk_get_idx(chk));
				return FALSE;
			}
			fu_progress_step_done(progress_child);
		}
		fu_device_sleep(device, 1000); /* ms */
	}
	fu_progress_step_done(progress);

	if (!fu_synaptics_rmi_device_enter_iep_mode(self,
						    FU_SYNAPTICS_RMI_DEVICE_FLAG_FORCE,
						    error))
		return FALSE;

	/* program the configuration image */
	if (!fu_synaptics_rmi_device_write(self,
					   f34->data_base,
					   req_addr,
					   FU_SYNAPTICS_RMI_DEVICE_FLAG_NONE,
					   error)) {
		g_prefix_error(error, "failed to 2nd write address zero: ");
		return FALSE;
	}
	for (guint i = 0; i < fu_chunk_array_length(chunks_cfg); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks_cfg, i);
		if (!fu_synaptics_rmi_v5_device_write_block(self,
							    RMI_F34_WRITE_CONFIG_BLOCK,
							    address,
							    fu_chunk_get_data(chk),
							    fu_chunk_get_data_sz(chk),
							    error)) {
			g_prefix_error(error,
				       "failed to write cfg block %u: ",
				       fu_chunk_get_idx(chk));
			return FALSE;
		}
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)fu_chunk_array_length(chunks_cfg));
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

gboolean
fu_synaptics_rmi_v5_device_setup(FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFunction *f34;
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash(self);
	guint8 flash_properties2 = 0;
	g_autoptr(GByteArray) f34_data0 = NULL;
	g_autoptr(GByteArray) f34_data2 = NULL;
	g_autoptr(GByteArray) buf_flash_properties2 = NULL;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function(self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* get bootloader ID */
	f34_data0 = fu_synaptics_rmi_device_read(self, f34->query_base, 0x2, error);
	if (f34_data0 == NULL) {
		g_prefix_error(error, "failed to read bootloader ID: ");
		return FALSE;
	}
	flash->bootloader_id[0] = f34_data0->data[0];
	flash->bootloader_id[1] = f34_data0->data[1];

	/* get flash properties1 */
	f34_data2 = fu_synaptics_rmi_device_read(self, f34->query_base + 0x2, 0x7, error);
	if (f34_data2 == NULL)
		return FALSE;

	if (f34_data2->data[0] & 0x80) {
		/* get flash properties2 */
		buf_flash_properties2 =
		    fu_synaptics_rmi_device_read(self, f34->query_base + 0x9, 1, error);
		if (buf_flash_properties2 == NULL) {
			g_prefix_error(error, "failed to read Flash Properties 2: ");
			return FALSE;
		}
		if (!fu_memread_uint8_safe(buf_flash_properties2->data,
					   buf_flash_properties2->len,
					   0x0, /* offset */
					   &flash_properties2,
					   error)) {
			g_prefix_error(error, "failed to parse Flash Properties 2: ");
			return FALSE;
		}
		if (flash_properties2 & 0x01) {
			guint16 sig_size = 0;
			g_autoptr(GByteArray) buf_rsa_key = NULL;
			buf_rsa_key = fu_synaptics_rmi_device_read(self,
								   f34->query_base + 0x9 + 0x1,
								   2,
								   error);
			if (buf_rsa_key == NULL) {
				g_prefix_error(error, "failed to read RSA key length: ");
				return FALSE;
			}
			if (!fu_memread_uint16_safe(buf_rsa_key->data,
						    buf_rsa_key->len,
						    0x0, /* offset */
						    &sig_size,
						    G_LITTLE_ENDIAN,
						    error)) {
				g_prefix_error(error, "failed to parse RSA key length: ");
				return FALSE;
			}
			fu_synaptics_rmi_device_set_sig_size(self, sig_size);
			fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
		} else {
			fu_synaptics_rmi_device_set_sig_size(self, 0);
		}
	}

	if (!fu_memread_uint16_safe(f34_data2->data,
				    f34_data2->len,
				    RMI_F34_BLOCK_SIZE_OFFSET,
				    &flash->block_size,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(f34_data2->data,
				    f34_data2->len,
				    RMI_F34_FW_BLOCKS_OFFSET,
				    &flash->block_count_fw,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(f34_data2->data,
				    f34_data2->len,
				    RMI_F34_CONFIG_BLOCKS_OFFSET,
				    &flash->block_count_cfg,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	flash->status_addr = f34->data_base + RMI_F34_BLOCK_DATA_OFFSET + flash->block_size;
	return TRUE;
}

gboolean
fu_synaptics_rmi_v5_device_query_status(FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFunction *f01;
	g_autoptr(GByteArray) f01_db = NULL;

	/* f01 */
	f01 = fu_synaptics_rmi_device_get_function(self, 0x01, error);
	if (f01 == NULL)
		return FALSE;
	f01_db = fu_synaptics_rmi_device_read(self, f01->data_base, 0x1, error);
	if (f01_db == NULL) {
		g_prefix_error(error, "failed to read the f01 data base: ");
		return FALSE;
	}
	if (f01_db->data[0] & 0x40) {
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}
	return TRUE;
}
