/*
 * Copyright (C) 2018 Synaptics
 * Copyright (C) 2018 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include <string.h>

#include "fu-common.h"

#include "fu-dell-dock-common.h"

#define I2C_MST_ADDRESS			0x72

/* MST registers */
#define MST_RC_TRIGGER_ADDR		0x2000fc
#define MST_CORE_MCU_BOOTLOADER_STS	0x20010c
#define MST_RC_COMMAND_ADDR		0x200110
#define MST_RC_OFFSET_ADDR		0x200114
#define MST_RC_LENGTH_ADDR		0x200118
#define MST_RC_DATA_ADDR		0x200120
#define MST_CORE_MCU_FW_VERSION		0x200160
#define MST_REG_QUAD_DISABLE		0x200fc0
#define MST_REG_HDCP22_DISABLE		0x200f90

/* MST remote control commands */
#define MST_CMD_ENABLE_REMOTE_CONTROL	0x1
#define MST_CMD_DISABLE_REMOTE_CONTROL	0x2
#define MST_CMD_CHECKSUM		0x11
#define MST_CMD_ERASE_FLASH		0x14
#define MST_CMD_WRITE_FLASH		0x20
#define MST_CMD_READ_FLASH		0x30
#define MST_CMD_WRITE_MEMORY		0x21
#define MST_CMD_READ_MEMORY		0x31

/* Arguments related to flashing */
#define FLASH_SECTOR_ERASE_4K		0x1000
#define FLASH_SECTOR_ERASE_32K		0x2000
#define FLASH_SECTOR_ERASE_64K		0x3000
#define EEPROM_TAG_OFFSET		0x1fff0
#define EEPROM_BANK_OFFSET		0x20000
#define EEPROM_ESM_OFFSET		0x40000

/* Flash offsets */
#define MST_BOARDID_OFFSET		0x10e

/* Remote control offsets */
#define MST_CHIPID_OFFSET 		0x1500

/* magic triggers */
#define MST_TRIGGER_WRITE 		0xf2
#define MST_TRIGGER_REBOOT		0xf5

/* IDs used in DELL_DOCK */
#define EXPECTED_CHIPID			0x5331

/* firmware file offsets */
#define MST_BLOB_VERSION_OFFSET		0x06F0

typedef enum {
	Bank0,
	Bank1,
	ESM,
} MSTBank;

typedef struct {
	guint start;
	guint length;
} MSTBankAttributes;

const MSTBankAttributes bank0_attributes = {
	.start = 0,
	.length = EEPROM_BANK_OFFSET,
};

const MSTBankAttributes bank1_attributes = {
	.start = EEPROM_BANK_OFFSET,
	.length = EEPROM_BANK_OFFSET,
};

const MSTBankAttributes esm_attributes = {
	.start = EEPROM_ESM_OFFSET,
	.length = 0x3ffff
};

FuHIDI2CParameters mst_base_settings = {
    .i2cslaveaddr = I2C_MST_ADDRESS,
    .regaddrlen = 0,
    .i2cspeed = I2C_SPEED_400K,
};

struct _FuDellDockMst {
	FuDevice			 parent_instance;
	FuDevice 			*symbiote;
	guint8				 unlock_target;
	guint64				 blob_major_offset;
	guint64				 blob_minor_offset;
	guint64				 blob_build_offset;
};

G_DEFINE_TYPE (FuDellDockMst, fu_dell_dock_mst, FU_TYPE_DEVICE)

/**
 * fu_dell_dock_mst_get_bank_attribs:
 * @bank: An MSTBank
 * @out (out): The MSTBankAttributes attribute that matches
 * @error: the #GError, or %NULL
 *
 * Returns a structure that corresponds to the attributes for a bank
 *
 * Returns: %TRUE for success
 **/
static gboolean
fu_dell_dock_mst_get_bank_attribs (MSTBank bank,
				   const MSTBankAttributes **out,
				   GError **error)
{
	switch (bank) {
	case Bank0:
		*out = &bank0_attributes;
		break;
	case Bank1:
		*out = &bank1_attributes;
		break;
	case ESM:
		*out = &esm_attributes;
		break;
	default:
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Invalid bank specified %u", bank);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_dock_mst_rc_command (FuDevice *symbiote,
			     guint8 cmd,
			     guint32 length,
			     guint32 offset,
			     const guint8 *data,
			     GError **error);

static gboolean
fu_dell_dock_mst_read_register (FuDevice *symbiote,
				guint32 address,
				gsize length,
				GBytes **bytes,
				GError **error)
{
	g_return_val_if_fail (symbiote != NULL, FALSE);
	g_return_val_if_fail (bytes != NULL, FALSE);
	g_return_val_if_fail (length <= 32, FALSE);

	/* write the offset we're querying */
	if (!fu_dell_dock_hid_i2c_write (symbiote, (guint8 *) &address, 4,
					 &mst_base_settings, error))
		return FALSE;

	/* read data for the result */
	if (!fu_dell_dock_hid_i2c_read (symbiote, 0, length, bytes,
					&mst_base_settings, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_dell_dock_mst_write_register (FuDevice *symbiote,
				 guint32 address,
				 guint8 *data,
				 gsize length,
				 GError **error)
{
	g_autofree guint8 *buffer = g_malloc0 (length + 4);

	g_return_val_if_fail (symbiote != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	memcpy (buffer, &address, 4);
	memcpy (buffer + 4, data, length);

	/* write the offset we're querying */
	return fu_dell_dock_hid_i2c_write (symbiote, buffer, length + 4,
					   &mst_base_settings, error);
}

static gboolean
fu_dell_dock_mst_query_active_bank (FuDevice *symbiote,
				    MSTBank *active,
				    GError **error)
{
	g_autoptr(GBytes) bytes = NULL;
	const guint32 *data = NULL;
	gsize length = 4;

	if (!fu_dell_dock_mst_read_register (symbiote, MST_CORE_MCU_BOOTLOADER_STS,
					     length, &bytes, error)) {
		g_prefix_error (error, "Failed to query active bank: ");
		return FALSE;
	}

	data = g_bytes_get_data (bytes, &length);
	if ((data[0] & (1 << 7)) || (data[0] & (1 << 30)))
		*active = Bank1;
	else
		*active = Bank0;
	g_debug ("MST: active bank is: %u", *active);

	return TRUE;
}

static gboolean
fu_dell_dock_mst_disable_remote_control (FuDevice *symbiote, GError **error)
{
	g_debug ("MST: Disabling remote control");
	return fu_dell_dock_mst_rc_command (symbiote,
					    MST_CMD_DISABLE_REMOTE_CONTROL,
					    0, 0,
					    NULL,
					    error);
}

static gboolean
fu_dell_dock_mst_enable_remote_control (FuDevice *symbiote, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	const gchar *data = "PRIUS";

	g_debug ("MST: Enabling remote control");
	if (!fu_dell_dock_mst_rc_command (symbiote,
					  MST_CMD_ENABLE_REMOTE_CONTROL,
					  5, 0,
					  (guint8 *) data,
					  &error_local)) {
		g_debug ("Failed to enable remote control: %s",
			 error_local->message);
		/* try to disable / re-enable */
		if (!fu_dell_dock_mst_disable_remote_control (symbiote, error))
			return FALSE;
		return fu_dell_dock_mst_enable_remote_control (symbiote, error);
	}
	return TRUE;
}

static gboolean
fu_dell_dock_trigger_rc_command (FuDevice *symbiote, GError **error)
{
	const guint8 *result = NULL;
	guint32 tmp;

	/* Trigger the write */
	tmp = MST_TRIGGER_WRITE;
	if (!fu_dell_dock_mst_write_register (symbiote,
					      MST_RC_TRIGGER_ADDR,
					      (guint8 *) &tmp, sizeof(guint32),
					      error)) {
		g_prefix_error (error, "Failed to write MST_RC_TRIGGER_ADDR: ");
		return FALSE;
	}
	/* poll for completion */
	tmp = 0xffff;
	for (guint i = 0; i < 1000; i++) {
		g_autoptr(GBytes) bytes = NULL;
		if (!fu_dell_dock_mst_read_register (symbiote,
						     MST_RC_COMMAND_ADDR,
						     sizeof(guint32), &bytes,
						     error)) {
			g_prefix_error (error,
					"Failed to poll MST_RC_COMMAND_ADDR");
			return FALSE;
		}
		result = g_bytes_get_data (bytes, NULL);
		/* complete */
		if ((result[2] & 0x80) == 0) {
			tmp = result[3];
			break;
		}
		g_usleep (2000);
	}
	switch (tmp) {
	/* need to enable remote control */
	case 4:
		return fu_dell_dock_mst_enable_remote_control (symbiote, error);
	/* error scenarios */
	case 3:
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     "Unknown error");
		return FALSE;
	case 2:
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     "Unsupported command");
		return FALSE;
	case 1:
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     "Invalid argument");
		return FALSE;
	/* success scenario */
	case 0:
		return TRUE;

	default:
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     "Command timed out or unknown failure: %x",
			     tmp);
		return FALSE;
	}
}

static gboolean
fu_dell_dock_mst_rc_command (FuDevice *symbiote,
			     guint8 cmd,
			     guint32 length,
			     guint32 offset,
			     const guint8 *data,
			     GError **error)
{
	/* 4 for cmd, 4 for offset, 4 for length, 4 for garbage */
	gint buffer_len = (data == NULL) ? 12 : length + 16;
	g_autofree guint8 *buffer = g_malloc0 (buffer_len);
	guint32 tmp;

	g_return_val_if_fail (symbiote != NULL, FALSE);

	/* command */
	tmp = (cmd | 0x80) << 16;
	memcpy (buffer, &tmp, 4);
	/* offset */
	memcpy (buffer + 4, &offset, 4);
	/* length */
	memcpy (buffer + 8, &length, 4);
	/* data */
	if (data != NULL)
		memcpy (buffer + 16, data, length);

	/* write the combined register stream */
	if (!fu_dell_dock_mst_write_register (symbiote, MST_RC_COMMAND_ADDR,
					      buffer, buffer_len, error))
		return FALSE;

	return fu_dell_dock_trigger_rc_command (symbiote, error);
}

static gboolean
fu_dell_dock_mst_read_chipid (FuDevice *symbiote,
			      guint16 *chip_id,
			      GError **error)
{
	g_autoptr(GBytes) bytes = NULL;
	const guint8 *data;
	gsize length = 4;

	g_return_val_if_fail (chip_id != NULL, FALSE);

	/* run an RC command to get data from memory */
	if (!fu_dell_dock_mst_rc_command (symbiote,
					  MST_CMD_READ_MEMORY,
					  length, MST_CHIPID_OFFSET,
					  NULL,
					  error))
		return FALSE;
	if (!fu_dell_dock_mst_read_register (symbiote,
					     MST_RC_DATA_ADDR,
					     length,
					     &bytes,
					     error))
		return FALSE;
	data = g_bytes_get_data (bytes, &length);
	*chip_id = (data[1] << 8) | data[2];

	return TRUE;
}

static gboolean
fu_dell_dock_mst_check_offset (guint8 byte, guint8 offset)
{
	if ((byte & offset) != 0)
		return TRUE;
	return FALSE;
}

static gboolean
fu_d19_mst_check_fw (FuDevice *symbiote, GError **error)
{
	g_autoptr(GBytes) bytes = NULL;
	const guint8 *data;
	gsize length = 4;

	if (!fu_dell_dock_mst_read_register (symbiote,
					     MST_CORE_MCU_BOOTLOADER_STS,
					     length, &bytes,
					     error))
		return FALSE;
	data = g_bytes_get_data (bytes, &length);

	g_debug ("MST: firmware check: %d",
		 fu_dell_dock_mst_check_offset (data[0], 0x01));
	g_debug ("MST: HDCP key check: %d",
		 fu_dell_dock_mst_check_offset (data[0], 0x02));
	g_debug ("MST: Config0  check: %d",
		 fu_dell_dock_mst_check_offset (data[0], 0x04));
	g_debug ("MST: Config1  check: %d",
		 fu_dell_dock_mst_check_offset (data[0], 0x08));

	if (fu_dell_dock_mst_check_offset (data[0], 0xF0))
		g_debug ("MST: running in bootloader");
	else
		g_debug ("MST: running in firmware");
	g_debug ("MST: Error code: %x", data[1]);
	g_debug ("MST: GPIO boot strap record: %d", data[2]);
	g_debug ("MST: Bootloader version number %x", data[3]);

	return TRUE;
}

static gboolean
fu_dell_dock_mst_checksum_bank (FuDevice *symbiote,
				GBytes *blob_fw,
				MSTBank bank,
				gboolean *checksum,
				GError **error)
{
	g_autoptr(GBytes) csum_bytes = NULL;
	const MSTBankAttributes *attribs = NULL;
	gsize length = 0;
	const guint8 *data = g_bytes_get_data (blob_fw, &length);
	guint32 payload_sum = 0;
	guint32 bank_sum = 0;

	g_return_val_if_fail (blob_fw != NULL, FALSE);
	g_return_val_if_fail (checksum != NULL, FALSE);

	if (!fu_dell_dock_mst_get_bank_attribs (bank, &attribs, error))
		return FALSE;

	/* bank is specified outside of payload */
	if (attribs->start + attribs->length > length) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     "Payload %u is bigger than bank %u",
			     attribs->start + attribs->length, bank);
		return FALSE;
	}

	/* checksum the file */
	for (guint i = attribs->start; i < attribs->length + attribs->start;
	     i++) {
		payload_sum += data[i];
	}
	g_debug ("MST: Payload checksum: 0x%x", payload_sum);

	/* checksum the bank */
	if (!fu_dell_dock_mst_rc_command (symbiote,
					  MST_CMD_CHECKSUM,
					  attribs->length, attribs->start,
					  NULL,
					  error)) {
		g_prefix_error (error, "Failed to checksum bank %u: ", bank);
		return FALSE;
	}
	/* read result from data register */
	if (!fu_dell_dock_mst_read_register (symbiote,
					     MST_RC_DATA_ADDR,
					     4, &csum_bytes, error))
		return FALSE;
	data = g_bytes_get_data (csum_bytes, NULL);
	bank_sum = GUINT32_FROM_LE (data[0] | data[1] << 8 | data[2] << 16 |
				    data[3] << 24);
	g_debug ("MST: Bank %u checksum: 0x%x", bank, bank_sum);

	*checksum = (bank_sum == payload_sum);

	return TRUE;
}

static gboolean
fu_dell_dock_mst_erase_bank (FuDevice *symbiote, MSTBank bank, GError **error)
{
	const MSTBankAttributes *attribs = NULL;
	guint32 sector;

	if (!fu_dell_dock_mst_get_bank_attribs (bank, &attribs, error))
		return FALSE;

	for (guint32 i = attribs->start; i < attribs->start + attribs->length;
	     i += 0x10000) {
		sector = FLASH_SECTOR_ERASE_64K | (i / 0x10000);
		g_debug ("MST: Erasing sector 0x%x", sector);
		if (!fu_dell_dock_mst_rc_command (symbiote,
						  MST_CMD_ERASE_FLASH,
						  4, 0,
						  (guint8 *) &sector,
						  error)) {
			g_prefix_error (
			    error, "Failed to erase sector 0x%x: ", sector);
			return FALSE;
		}
	}
	g_debug ("MST: Waiting for flash clear to settle");
	g_usleep (5000000);

	return TRUE;
}

static gboolean
fu_dell_dock_write_flash_bank (FuDevice *device,
			       GBytes *blob_fw,
			       MSTBank bank,
			       GError **error)
{
	FuDellDockMst *self = FU_DELL_DOCK_MST (device);
	const MSTBankAttributes *attribs = NULL;
	gsize write_size = 32;
	guint end;
	const guint8 *data = g_bytes_get_data (blob_fw, NULL);

	g_return_val_if_fail (blob_fw != NULL, FALSE);

	if (!fu_dell_dock_mst_get_bank_attribs (bank, &attribs, error))
		return FALSE;
	end = attribs->start + attribs->length;

	g_debug ("MST: Writing payload to bank %u", bank);
	for (guint i = attribs->start; i < end; i += write_size) {
		if (!fu_dell_dock_mst_rc_command (self->symbiote,
						  MST_CMD_WRITE_FLASH,
						  write_size, i,
						  data + i,
						  error)) {
			g_prefix_error (
			    error,
			    "Failed to write bank %u payload offset 0x%x: ",
			    bank, i);
			return FALSE;
		}
		fu_device_set_progress_full (device,
					     i - attribs->start,
					     end - attribs->start);
	}

	return TRUE;
}

static gboolean
fu_dell_dock_mst_stop_esm (FuDevice *symbiote, GError **error)
{
	g_autoptr(GBytes) quad_bytes = NULL;
	g_autoptr(GBytes) hdcp_bytes = NULL;
	guint32 payload = 0x21;
	gsize length = sizeof(guint32);
	const guint8 *data;
	guint8 data_out[sizeof(guint32)];

	/* disable ESM first */
	if (!fu_dell_dock_mst_rc_command (symbiote,
					  MST_CMD_WRITE_MEMORY,
					  length, MST_RC_TRIGGER_ADDR,
					  (guint8 *) &payload,
					  error))
		return FALSE;

	/* waiting for ESM exit */
	g_usleep(200);

	/* disable QUAD mode */
	if (!fu_dell_dock_mst_rc_command (symbiote,
					  MST_CMD_READ_MEMORY,
					  length, MST_REG_QUAD_DISABLE,
					  NULL,
					  error))
		return FALSE;

	if (!fu_dell_dock_mst_read_register (symbiote,
					     MST_RC_DATA_ADDR,
					     length, &quad_bytes,
					     error))
		return FALSE;

	data = g_bytes_get_data (quad_bytes, &length);
	memcpy (data_out, data, length);
	data_out[0] = 0x00;
	if (!fu_dell_dock_mst_rc_command (symbiote,
					  MST_CMD_WRITE_MEMORY,
					  length, MST_REG_QUAD_DISABLE,
					  data_out, error))
		return FALSE;

	/* disable HDCP2.2 */
	if (!fu_dell_dock_mst_rc_command (symbiote,
					  MST_CMD_READ_MEMORY,
					  length, MST_REG_HDCP22_DISABLE,
					  NULL,
					  error))
		return FALSE;

	if (!fu_dell_dock_mst_read_register (symbiote,
					     MST_RC_DATA_ADDR,
					     length,
					     &hdcp_bytes,
					     error))
		return FALSE;

	data = g_bytes_get_data (hdcp_bytes, &length);
	memcpy (data_out, data, length);
	data_out[0] = data[0] & (1 << 2);
	if (!fu_dell_dock_mst_rc_command (symbiote,
					  MST_CMD_WRITE_MEMORY,
					  length, MST_REG_HDCP22_DISABLE,
					  data_out,
					  error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_dell_dock_mst_invalidate_bank (FuDevice *symbiote, MSTBank bank_in_use,
				  GError **error)
{
	const MSTBankAttributes *attribs;
	g_autoptr(GBytes) bytes = NULL;
	const guint8 *crc_tag;
	const guint8 *new_tag;
	guint32 crc_offset;
	guint retries = 2;

	if (!fu_dell_dock_mst_get_bank_attribs (bank_in_use, &attribs, error)) {
		g_prefix_error (error, "unable to invalidate bank: ");
		return FALSE;
	}
	/* we need to write 4 byte increments over I2C so this differs from DP aux */
	crc_offset = attribs->start + EEPROM_TAG_OFFSET + 12;

	/* Read CRC byte to flip */
	if (!fu_dell_dock_mst_rc_command (symbiote,
					  MST_CMD_READ_FLASH,
					  4, crc_offset,
					  NULL,
					  error)) {

		g_prefix_error (error, "failed to read tag from flash: ");
		return FALSE;
	}
	if (!fu_dell_dock_mst_read_register (symbiote,
					     MST_RC_DATA_ADDR,
					     1,
					     &bytes,
					     error)) {
		return FALSE;
	}
	crc_tag = g_bytes_get_data (bytes, NULL);
	g_debug ("CRC byte is currently 0x%x", crc_tag[3]);

	for (guint32 retries_cnt = 0; ; retries_cnt++) {
		g_autoptr(GBytes) bytes_new = NULL;
		/* CRC8 is not 0xff, erase last 4k of bank# */
		if (crc_tag[3] != 0xff) {
			guint32 sector = FLASH_SECTOR_ERASE_4K +
					(attribs->start + attribs->length - 0x1000) / 0x1000;
			g_debug ("Erasing 4k from sector 0x%x invalidate bank %u",
				 sector, bank_in_use);
			/* offset for last 4k of bank# */
			if (!fu_dell_dock_mst_rc_command (symbiote,
							  MST_CMD_ERASE_FLASH,
							  4, 0,
							  (guint8 *) &sector,
							  error)) {

				g_prefix_error (error,
						"failed to erase sector 0x%x: ",
						sector);
				return FALSE;
			}
		/* CRC8 is 0xff, set it to 0x00 */
		} else {
			guint32 write = 0x00;
			g_debug ("Writing 0x00 byte to 0x%x to invalidate bank %u",
				 crc_offset, bank_in_use);
			if (!fu_dell_dock_mst_rc_command (symbiote,
							  MST_CMD_WRITE_FLASH,
							  4, crc_offset,
							  (guint8*) &write,
							  error)) {

				g_prefix_error (error, "failed to clear CRC byte: ");
				return FALSE;
			}
		}
		/* re-read for comparison */
		if (!fu_dell_dock_mst_rc_command (symbiote,
						  MST_CMD_READ_FLASH,
						  4, crc_offset,
						  NULL,
						  error)) {

			g_prefix_error (error, "failed to read tag from flash: ");
			return FALSE;
		}
		if (!fu_dell_dock_mst_read_register (symbiote,
						     MST_RC_DATA_ADDR,
						     4, &bytes_new,
						     error)) {
			return FALSE;
		}
		new_tag = g_bytes_get_data (bytes_new, NULL);
		g_debug ("CRC byte is currently 0x%x", new_tag[3]);

		/* tag successfully cleared */
		if ((new_tag[3] == 0xff && crc_tag[3] != 0xff) ||
		    (new_tag[3] == 0x00 && crc_tag[3] == 0xff)) {
			break;
		}
		if (retries_cnt > retries) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "set tag invalid fail (new 0x%x; old 0x%x)",
				     new_tag[3], crc_tag[3]);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_dell_dock_mst_write_fw (FuDevice *device,
			   FuFirmware *firmware,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuDellDockMst *self = FU_DELL_DOCK_MST (device);
	MSTBank bank_in_use = 0;
	guint retries = 2;
	gboolean checksum = FALSE;
	guint8 order[2] = {ESM, Bank0};
	guint16 chip_id;
	const guint8 *data;
	g_autofree gchar *dynamic_version = NULL;
	g_autoptr(GBytes) fw = NULL;

	g_return_val_if_fail (device != NULL, FALSE);
	g_return_val_if_fail (FU_IS_FIRMWARE (firmware), FALSE);
	g_return_val_if_fail (self->symbiote != NULL, FALSE);

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;
	data = g_bytes_get_data (fw, NULL);

	dynamic_version = g_strdup_printf ("%02x.%02x.%02x",
					   data[self->blob_major_offset],
					   data[self->blob_minor_offset],
					   data[self->blob_build_offset]);
	g_debug ("writing MST firmware version %s", dynamic_version);

	/* determine the flash order */
	if (!fu_dell_dock_mst_query_active_bank (self->symbiote, &bank_in_use, error))
		return FALSE;

	if (bank_in_use == Bank0)
		order[1] = Bank1;

	/* enable remote control */
	if (!fu_dell_dock_mst_enable_remote_control (self->symbiote, error))
		return FALSE;

	/* Read Synaptics MST chip ID */
	if (!fu_dell_dock_mst_read_chipid (self->symbiote, &chip_id,
					error))
		return FALSE;
	if (chip_id != EXPECTED_CHIPID) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     "Unknown MST chip found %x", chip_id);
		return FALSE;
	}

	/* ESM needs special handling during flash process*/
	if (!fu_dell_dock_mst_stop_esm (self->symbiote, error))
		return FALSE;

	/* Write each bank in order */
	for (guint phase = 0; phase < 2; phase++) {
		g_debug ("MST: Checking bank %u", order[phase]);
		if (!fu_dell_dock_mst_checksum_bank (self->symbiote,
						     fw,
						     order[phase],
						     &checksum, error))
			return FALSE;
		if (checksum) {
			g_debug ("MST: bank %u is already up to date", order[phase]);
			continue;
		}
		g_debug ("MST: bank %u needs to be updated", order[phase]);
		for (guint i = 0; i < retries; i++) {
			fu_device_set_progress_full (device, 0, 100);
			fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
			if (!fu_dell_dock_mst_erase_bank (self->symbiote,
							  order[phase],
							  error))
				return FALSE;
			fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
			if (!fu_dell_dock_write_flash_bank (device, fw,
						       order[phase], error))
				return FALSE;
			if (!fu_dell_dock_mst_checksum_bank (self->symbiote,
							     fw,
							     order[phase],
							     &checksum,
							     error))
				return FALSE;
			fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);
			if (!checksum) {
				g_debug (
				    "MST: Failed to verify checksum on bank %u",
				    order[phase]);
				continue;
			}
			g_debug ("MST: Bank %u successfully flashed", order[phase]);
			break;
		}
		/* failed after all our retries */
		if (!checksum) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				    "Failed to write to bank %u", order[phase]);
			return FALSE;
		}
	}
	/* invalidate the previous bank */
	if (!fu_dell_dock_mst_invalidate_bank (self->symbiote, bank_in_use, error)) {
		g_prefix_error (error, "failed to invalidate bank %u: ", bank_in_use);
		return FALSE;
	}

	/* dock will reboot to re-read; this is to appease the daemon */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_set_version (device, dynamic_version, FWUPD_VERSION_FORMAT_TRIPLET);

	/* disable remote control now */
	return fu_dell_dock_mst_disable_remote_control (self->symbiote, error);
}

static gboolean
fu_dell_dock_mst_set_quirk_kv (FuDevice *device,
			       const gchar *key,
			       const gchar *value,
			       GError **error)
{
	FuDellDockMst *self = FU_DELL_DOCK_MST (device);

	if (g_strcmp0 (key, "DellDockUnlockTarget") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT8) {
			self->unlock_target = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid DellDockUnlockTarget");
		return FALSE;
	}
	if (g_strcmp0 (key, "DellDockBlobMajorOffset") == 0) {
		self->blob_major_offset = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "DellDockBlobMinorOffset") == 0) {
		self->blob_minor_offset = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "DellDockBlobBuildOffset") == 0) {
		self->blob_build_offset = fu_common_strtoull (value);
		return TRUE;
	}
	else if (g_strcmp0 (key, "DellDockInstallDurationI2C") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		fu_device_set_install_duration (device, tmp);
		return TRUE;
	}

	/* failed */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}

static gboolean
fu_dell_dock_mst_setup (FuDevice *device, GError **error)
{
	FuDellDockMst *self = FU_DELL_DOCK_MST (device);
	FuDevice *parent;
	const gchar *version;

	/* sanity check that we can talk to MST */
	if (!fu_d19_mst_check_fw (self->symbiote, error))
		return FALSE;

	/* set version from EC if we know it */
	parent = fu_device_get_parent (device);
	version = fu_dell_dock_ec_get_mst_version (parent);
	if (version != NULL)
		fu_device_set_version (device, version, FWUPD_VERSION_FORMAT_TRIPLET);

	fu_dell_dock_clone_updatable (device);

	return TRUE;
}

static gboolean
fu_dell_dock_mst_probe (FuDevice *device, GError **error)
{
	fu_device_set_logical_id (FU_DEVICE (device), "mst");

	return TRUE;
}

static gboolean
fu_dell_dock_mst_open (FuDevice *device, GError **error)
{
	FuDellDockMst *self = FU_DELL_DOCK_MST (device);
	FuDevice *parent = fu_device_get_parent (device);

	g_return_val_if_fail (self->unlock_target != 0, FALSE);
	g_return_val_if_fail (parent != NULL, FALSE);

	if (self->symbiote == NULL)
		self->symbiote = g_object_ref (fu_dell_dock_ec_get_symbiote (parent));

	if (!fu_device_open (self->symbiote, error))
		return FALSE;

	/* open up access to controller bus */
	if (!fu_dell_dock_set_power (device, self->unlock_target, TRUE, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_dell_dock_mst_close (FuDevice *device, GError **error)
{
	FuDellDockMst *self = FU_DELL_DOCK_MST (device);

	/* close access to controller bus */
	if (!fu_dell_dock_set_power (device, self->unlock_target, FALSE, error))
		return FALSE;

	return fu_device_close (self->symbiote, error);
}

static void
fu_dell_dock_mst_finalize (GObject *object)
{
	FuDellDockMst *self = FU_DELL_DOCK_MST (object);
	g_object_unref (self->symbiote);
	G_OBJECT_CLASS (fu_dell_dock_mst_parent_class)->finalize (object);
}

static void
fu_dell_dock_mst_init (FuDellDockMst *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "com.synaptics.mst");
}

static void
fu_dell_dock_mst_class_init (FuDellDockMstClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_dell_dock_mst_finalize;
	klass_device->probe = fu_dell_dock_mst_probe;
	klass_device->open = fu_dell_dock_mst_open;
	klass_device->close = fu_dell_dock_mst_close;
	klass_device->setup = fu_dell_dock_mst_setup;
	klass_device->probe = fu_dell_dock_mst_probe;
	klass_device->write_firmware = fu_dell_dock_mst_write_fw;
	klass_device->set_quirk_kv = fu_dell_dock_mst_set_quirk_kv;
}

FuDellDockMst *
fu_dell_dock_mst_new (void)
{
	FuDellDockMst *device = NULL;
	device = g_object_new (FU_TYPE_DELL_DOCK_MST, NULL);
	return device;
}
