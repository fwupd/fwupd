/*
 * Copyright (C) 2012-2014 Andrew Duggan
 * Copyright (C) 2012-2019 Synaptics Inc.
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-chunk.h"
#include "fu-common.h"
#include "fu-synaptics-rmi-v5-device.h"

#include "fwupd-error.h"

#define RMI_F34_WRITE_FW_BLOCK				0x02
#define RMI_F34_ERASE_ALL				0x03
#define RMI_F34_WRITE_LOCKDOWN_BLOCK			0x04
#define RMI_F34_WRITE_CONFIG_BLOCK			0x06
#define RMI_F34_ENABLE_FLASH_PROG			0x0f

#define RMI_F34_BLOCK_SIZE_OFFSET			1
#define RMI_F34_FW_BLOCKS_OFFSET			3
#define RMI_F34_CONFIG_BLOCKS_OFFSET			5

#define RMI_F34_ERASE_WAIT_MS				(5 * 1000)	/* ms */

gboolean
fu_synaptics_rmi_v5_device_detach (FuDevice *device, GError **error)
{
	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash (self);
	g_autoptr(GByteArray) enable_req = g_byte_array_new ();

	/* disable interrupts */
	if (!fu_synaptics_rmi_device_disable_irqs (self, error))
		return FALSE;

	/* unlock bootloader and rebind kernel driver */
	if (!fu_synaptics_rmi_device_write_bootloader_id (self, error))
		return FALSE;
	fu_byte_array_append_uint8 (enable_req, RMI_F34_ENABLE_FLASH_PROG);
	if (!fu_synaptics_rmi_device_write (self,
					    flash->status_addr,
					    enable_req,
					    error)) {
		g_prefix_error (error, "failed to enable programming: ");
		return FALSE;
	}

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	g_usleep (1000 * RMI_F34_ENABLE_WAIT_MS);
	return fu_synaptics_rmi_device_rebind_driver (self, error);
}

static gboolean
fu_synaptics_rmi_v5_device_erase_all (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFunction *f34;
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash (self);
	g_autoptr(GByteArray) erase_cmd = g_byte_array_new ();

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* all other versions */
	fu_byte_array_append_uint8 (erase_cmd, RMI_F34_ERASE_ALL);
	if (!fu_synaptics_rmi_device_write (self,
					    flash->status_addr,
					    erase_cmd,
					    error)) {
		g_prefix_error (error, "failed to erase core config: ");
		return FALSE;
	}
	g_usleep (1000 * RMI_F34_ENABLE_WAIT_MS);
	if (!fu_synaptics_rmi_device_wait_for_idle (self,
						    RMI_F34_ERASE_WAIT_MS,
						    RMI_DEVICE_WAIT_FOR_IDLE_FLAG_REFRESH_F34,
						    error)) {
		g_prefix_error (error, "failed to wait for idle for erase: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_rmi_v5_device_write_block (FuSynapticsRmiDevice *self,
					guint8 cmd,
					guint32 address,
					const guint8 *data,
					gsize datasz,
					GError **error)
{
	g_autoptr(GByteArray) req = g_byte_array_new ();

	g_byte_array_append (req, data, datasz);
	fu_byte_array_append_uint8 (req, cmd);
	if (!fu_synaptics_rmi_device_write (self, address, req, error)) {
		g_prefix_error (error, "failed to write block @0x%x: ", address);
		return FALSE;
	}
	if (!fu_synaptics_rmi_device_wait_for_idle (self,
						    RMI_F34_IDLE_WAIT_MS,
						    RMI_DEVICE_WAIT_FOR_IDLE_FLAG_NONE,
						    error)) {
		g_prefix_error (error, "failed to wait for idle @0x%x: ", address);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_synaptics_rmi_v5_device_write_firmware (FuDevice *device,
					   FuFirmware *firmware,
					   FwupdInstallFlags flags,
					   GError **error)
{

	FuSynapticsRmiDevice *self = FU_SYNAPTICS_RMI_DEVICE (device);
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash (self);
	FuSynapticsRmiFunction *f34;
	guint32 address;
	g_autoptr(GBytes) bytes_bin = NULL;
	g_autoptr(GBytes) bytes_cfg = NULL;
	g_autoptr(GPtrArray) chunks_bin = NULL;
	g_autoptr(GPtrArray) chunks_cfg = NULL;
	g_autoptr(GByteArray) req_addr = g_byte_array_new ();

	/* we should be in bootloader mode now, but check anyway */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not bootloader, perhaps need detach?!");
		return FALSE;
	}

	/* check is idle */
	if (!fu_synaptics_rmi_device_wait_for_idle (self, 0,
						    RMI_DEVICE_WAIT_FOR_IDLE_FLAG_REFRESH_F34,
						    error)) {
		g_prefix_error (error, "not idle: ");
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

	/* disable powersaving */
	if (!fu_synaptics_rmi_device_disable_sleep (self, error)) {
		g_prefix_error (error, "failed to disable sleep: ");
		return FALSE;
	}

	/* unlock again */
	if (!fu_synaptics_rmi_device_write_bootloader_id (self, error)) {
		g_prefix_error (error, "failed to unlock again: ");
		return FALSE;
	}

	/* erase all */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_synaptics_rmi_v5_device_erase_all (self, error)) {
		g_prefix_error (error, "failed to erase all: ");
		return FALSE;
	}

	/* write initial address */
	fu_byte_array_append_uint16 (req_addr, 0x0, G_LITTLE_ENDIAN);
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_synaptics_rmi_device_write (self, f34->data_base, req_addr, error)) {
		g_prefix_error (error, "failed to write 1st address zero: ");
		return FALSE;
	}

	/* write each block */
	if (f34->function_version == 0x01)
		address = f34->data_base + RMI_F34_BLOCK_DATA_V1_OFFSET;
	else
		address = f34->data_base + RMI_F34_BLOCK_DATA_OFFSET;
	chunks_bin = fu_chunk_array_new_from_bytes (bytes_bin,
						    0x00,	/* start addr */
						    0x00,	/* page_sz */
						    flash->block_size);
	chunks_cfg = fu_chunk_array_new_from_bytes (bytes_cfg,
						    0x00,	/* start addr */
						    0x00,	/* page_sz */
						    flash->block_size);
	for (guint i = 0; i < chunks_bin->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks_bin, i);
		if (!fu_synaptics_rmi_v5_device_write_block (self,
							     RMI_F34_WRITE_FW_BLOCK,
							     address,
							     chk->data,
							     chk->data_sz,
							     error)) {
			g_prefix_error (error, "failed to write bin block %u: ", chk->idx);
			return FALSE;
		}
		fu_device_set_progress_full (device, (gsize) i,
					     (gsize) chunks_bin->len + chunks_cfg->len);
	}

	/* program the configuration image */
	if (!fu_synaptics_rmi_device_write (self, f34->data_base, req_addr, error)) {
		g_prefix_error (error, "failed to 2nd write address zero: ");
		return FALSE;
	}
	for (guint i = 0; i < chunks_cfg->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks_cfg, i);
		if (!fu_synaptics_rmi_v5_device_write_block (self,
							     RMI_F34_WRITE_CONFIG_BLOCK,
							     address,
							     chk->data,
							     chk->data_sz,
							     error)) {
			g_prefix_error (error, "failed to write cfg block %u: ", chk->idx);
			return FALSE;
		}
		fu_device_set_progress_full (device,
					     (gsize) chunks_bin->len + i,
					     (gsize) chunks_bin->len + chunks_cfg->len);
	}

	/* success */
	return TRUE;
}

gboolean
fu_synaptics_rmi_v5_device_setup (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFunction *f34;
	FuSynapticsRmiFlash *flash = fu_synaptics_rmi_device_get_flash (self);
	g_autoptr(GByteArray) f34_data0 = NULL;
	g_autoptr(GByteArray) f34_data2 = NULL;

	/* f34 */
	f34 = fu_synaptics_rmi_device_get_function (self, 0x34, error);
	if (f34 == NULL)
		return FALSE;

	/* get bootloader ID */
	f34_data0 = fu_synaptics_rmi_device_read (self, f34->query_base, 0x2, error);
	if (f34_data0 == NULL) {
		g_prefix_error (error, "failed to read bootloader ID: ");
		return FALSE;
	}
	flash->bootloader_id[0] = f34_data0->data[0];
	flash->bootloader_id[1] = f34_data0->data[1];

	/* get flash properties */
	f34_data2 = fu_synaptics_rmi_device_read (self, f34->query_base + 0x2, 0x7, error);
	if (f34_data2 == NULL)
		return FALSE;
	flash->block_size = fu_common_read_uint16 (f34_data2->data + RMI_F34_BLOCK_SIZE_OFFSET, G_LITTLE_ENDIAN);
	flash->block_count_fw = fu_common_read_uint16 (f34_data2->data + RMI_F34_FW_BLOCKS_OFFSET, G_LITTLE_ENDIAN);
	flash->block_count_cfg = fu_common_read_uint16 (f34_data2->data + RMI_F34_CONFIG_BLOCKS_OFFSET, G_LITTLE_ENDIAN);
	flash->status_addr = f34->data_base + RMI_F34_BLOCK_DATA_OFFSET + flash->block_size;
	return TRUE;
}

gboolean
fu_synaptics_rmi_v5_device_query_status (FuSynapticsRmiDevice *self, GError **error)
{
	FuSynapticsRmiFunction *f01;
	g_autoptr(GByteArray) f01_db = NULL;

	/* f01 */
	f01 = fu_synaptics_rmi_device_get_function (self, 0x01, error);
	if (f01 == NULL)
		return FALSE;
	f01_db = fu_synaptics_rmi_device_read (self, f01->data_base, 0x1, error);
	if (f01_db == NULL) {
		g_prefix_error (error, "failed to read the f01 data base: ");
		return FALSE;
	}
	if (f01_db->data[0] & 0x40) {
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}
	return TRUE;
}
