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

#include <fwupdplugin.h>

#include <string.h>

#include "fu-dell-dock-common.h"

#define I2C_MST_ADDRESS 0x72

/* Panamera MST registers */
#define PANAMERA_MST_RC_TRIGGER_ADDR	     0x2000fc
#define PANAMERA_MST_CORE_MCU_BOOTLOADER_STS 0x20010c
#define PANAMERA_MST_RC_COMMAND_ADDR	     0x200110
#define PANAMERA_MST_RC_OFFSET_ADDR	     0x200114
#define PANAMERA_MST_RC_LENGTH_ADDR	     0x200118
#define PANAMERA_MST_RC_DATA_ADDR	     0x200120
#define PANAMERA_MST_CORE_MCU_FW_VERSION     0x200160
#define PANAMERA_MST_REG_QUAD_DISABLE	     0x200fc0
#define PANAMERA_MST_REG_HDCP22_DISABLE	     0x200f90

/* Cayenne MST registers */
#define CAYENNE_MST_RC_TRIGGER_ADDR	    0x2020021C
#define CAYENNE_MST_CORE_MCU_BOOTLOADER_STS 0x2020022C
#define CAYENNE_MST_RC_COMMAND_ADDR	    0x20200280
#define CAYENNE_MST_RC_OFFSET_ADDR	    0x20200284
#define CAYENNE_MST_RC_LENGTH_ADDR	    0x20200288
#define CAYENNE_MST_RC_DATA_ADDR	    0x20200290

/* MST remote control commands */
#define MST_CMD_ENABLE_REMOTE_CONTROL  0x1
#define MST_CMD_DISABLE_REMOTE_CONTROL 0x2
#define MST_CMD_CHECKSUM	       0x11
#define MST_CMD_ERASE_FLASH	       0x14
#define MST_CMD_WRITE_FLASH	       0x20
#define MST_CMD_READ_FLASH	       0x30
#define MST_CMD_WRITE_MEMORY	       0x21
#define MST_CMD_READ_MEMORY	       0x31

/* Cayenne specific remote control commands */
#define MST_CMD_CRC16_CHECKSUM 0x17
#define MST_CMD_ACTIVATE_FW    0x18

/* Arguments related to flashing */
#define FLASH_SECTOR_ERASE_4K  0x1000
#define FLASH_SECTOR_ERASE_32K 0x2000
#define FLASH_SECTOR_ERASE_64K 0x3000
#define EEPROM_TAG_OFFSET      0x1fff0
#define EEPROM_BANK_OFFSET     0x20000
#define EEPROM_ESM_OFFSET      0x40000

/* Flash offsets */
#define MST_BOARDID_OFFSET 0x10e

/* Remote control offsets */
#define MST_CHIPID_OFFSET 0x1500

/* magic triggers */
#define MST_TRIGGER_WRITE  0xf2
#define MST_TRIGGER_REBOOT 0xf5

/* IDs used in DELL_DOCK */
#define EXPECTED_CHIPID 0x5331

/* firmware file offsets */
#define MST_BLOB_VERSION_OFFSET 0x06F0

typedef enum {
	Panamera_mst,
	Cayenne_mst,
	Unknown,
} MSTType;

typedef enum {
	Bank0,
	Bank1,
	ESM,
	Cayenne,
} MSTBank;

typedef struct {
	guint start;
	guint length;
	guint checksum_cmd;
} MSTBankAttributes;

const MSTBankAttributes bank0_attributes = {
    .start = 0,
    .length = EEPROM_BANK_OFFSET,
    .checksum_cmd = MST_CMD_CHECKSUM,
};

const MSTBankAttributes bank1_attributes = {
    .start = EEPROM_BANK_OFFSET,
    .length = EEPROM_BANK_OFFSET,
    .checksum_cmd = MST_CMD_CHECKSUM,
};

const MSTBankAttributes esm_attributes = {
    .start = EEPROM_ESM_OFFSET,
    .length = 0x3ffff,
    .checksum_cmd = MST_CMD_CHECKSUM,
};

const MSTBankAttributes cayenne_attributes = {
    .start = 0,
    .length = 0x50000,
    .checksum_cmd = MST_CMD_CRC16_CHECKSUM,
};

FuHIDI2CParameters mst_base_settings = {
    .i2ctargetaddr = I2C_MST_ADDRESS,
    .regaddrlen = 0,
    .i2cspeed = I2C_SPEED_400K,
};

struct _FuDellDockMst {
	FuDevice parent_instance;
	guint8 unlock_target;
	guint64 blob_major_offset;
	guint64 blob_minor_offset;
	guint64 blob_build_offset;
	guint32 mst_rc_trigger_addr;
	guint32 mst_rc_command_addr;
	guint32 mst_rc_data_addr;
	guint32 mst_core_mcu_bootloader_addr;
};

G_DEFINE_TYPE(FuDellDockMst, fu_dell_dock_mst, FU_TYPE_DEVICE)

/**
 * fu_dell_dock_mst_get_bank_attribs:
 * @bank: the MSTBank
 * @out (out): the MSTBankAttributes attribute that matches
 * @error: (nullable): optional return location for an error
 *
 * Returns a structure that corresponds to the attributes for a bank
 *
 * Returns: %TRUE for success
 **/
static gboolean
fu_dell_dock_mst_get_bank_attribs(MSTBank bank, const MSTBankAttributes **out, GError **error)
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
	case Cayenne:
		*out = &cayenne_attributes;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Invalid bank specified %u",
			    bank);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_dock_mst_rc_command(FuDevice *device,
			    guint8 cmd,
			    guint32 length,
			    guint32 offset,
			    const guint8 *data,
			    GError **error);

static gboolean
fu_dell_dock_mst_read_register(FuDevice *proxy,
			       guint32 address,
			       gsize length,
			       GBytes **bytes,
			       GError **error)
{
	g_return_val_if_fail(proxy != NULL, FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);
	g_return_val_if_fail(length <= 32, FALSE);

	/* write the offset we're querying */
	if (!fu_dell_dock_hid_i2c_write(proxy, (guint8 *)&address, 4, &mst_base_settings, error))
		return FALSE;

	/* read data for the result */
	if (!fu_dell_dock_hid_i2c_read(proxy, 0, length, bytes, &mst_base_settings, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_dell_dock_mst_write_register(FuDevice *proxy,
				guint32 address,
				guint8 *data,
				gsize length,
				GError **error)
{
	g_autofree guint8 *buffer = g_malloc0(length + 4);

	g_return_val_if_fail(proxy != NULL, FALSE);
	g_return_val_if_fail(data != NULL, FALSE);

	memcpy(buffer, &address, 4);
	memcpy(buffer + 4, data, length);

	/* write the offset we're querying */
	return fu_dell_dock_hid_i2c_write(proxy, buffer, length + 4, &mst_base_settings, error);
}

static gboolean
fu_dell_dock_mst_query_active_bank(FuDevice *proxy, MSTBank *active, GError **error)
{
	g_autoptr(GBytes) bytes = NULL;
	const guint32 *data = NULL;
	gsize length = 4;

	if (!fu_dell_dock_mst_read_register(proxy,
					    PANAMERA_MST_CORE_MCU_BOOTLOADER_STS,
					    length,
					    &bytes,
					    error)) {
		g_prefix_error(error, "Failed to query active bank: ");
		return FALSE;
	}

	data = g_bytes_get_data(bytes, &length);
	if ((data[0] & (1 << 7)) || (data[0] & (1 << 30)))
		*active = Bank1;
	else
		*active = Bank0;
	g_debug("MST: active bank is: %u", *active);

	return TRUE;
}

static gboolean
fu_dell_dock_mst_disable_remote_control(FuDevice *device, GError **error)
{
	g_debug("MST: Disabling remote control");
	return fu_dell_dock_mst_rc_command(device,
					   MST_CMD_DISABLE_REMOTE_CONTROL,
					   0,
					   0,
					   NULL,
					   error);
}

static gboolean
fu_dell_dock_mst_enable_remote_control(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	const gchar *data = "PRIUS";

	g_debug("MST: Enabling remote control");
	if (!fu_dell_dock_mst_rc_command(device,
					 MST_CMD_ENABLE_REMOTE_CONTROL,
					 5,
					 0,
					 (guint8 *)data,
					 &error_local)) {
		g_debug("Failed to enable remote control: %s", error_local->message);
		/* try to disable / re-enable */
		if (!fu_dell_dock_mst_disable_remote_control(device, error))
			return FALSE;
		return fu_dell_dock_mst_enable_remote_control(device, error);
	}
	return TRUE;
}

static gboolean
fu_dell_dock_trigger_rc_command(FuDevice *device, GError **error)
{
	const guint8 *result = NULL;
	FuDevice *proxy = fu_device_get_proxy(device);
	FuDellDockMst *self = FU_DELL_DOCK_MST(device);
	guint32 tmp;

	/* Trigger the write */
	tmp = MST_TRIGGER_WRITE;
	if (!fu_dell_dock_mst_write_register(proxy,
					     self->mst_rc_trigger_addr,
					     (guint8 *)&tmp,
					     sizeof(guint32),
					     error)) {
		g_prefix_error(error, "Failed to write MST_RC_TRIGGER_ADDR: ");
		return FALSE;
	}
	/* poll for completion */
	tmp = 0xffff;
	for (guint i = 0; i < 1000; i++) {
		g_autoptr(GBytes) bytes = NULL;
		if (!fu_dell_dock_mst_read_register(proxy,
						    self->mst_rc_command_addr,
						    sizeof(guint32),
						    &bytes,
						    error)) {
			g_prefix_error(error, "Failed to poll MST_RC_COMMAND_ADDR");
			return FALSE;
		}
		result = g_bytes_get_data(bytes, NULL);
		/* complete */
		if ((result[2] & 0x80) == 0) {
			tmp = result[3];
			break;
		}
		g_usleep(2000);
	}
	switch (tmp) {
	/* need to enable remote control */
	case 4:
		return fu_dell_dock_mst_enable_remote_control(device, error);
	/* error scenarios */
	case 3:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unknown error");
		return FALSE;
	case 2:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unsupported command");
		return FALSE;
	case 1:
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Invalid argument");
		return FALSE;
	/* success scenario */
	case 0:
		return TRUE;

	default:
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Command timed out or unknown failure: %x",
			    tmp);
		return FALSE;
	}
}

static gboolean
fu_dell_dock_mst_rc_command(FuDevice *device,
			    guint8 cmd,
			    guint32 length,
			    guint32 offset,
			    const guint8 *data,
			    GError **error)
{
	/* 4 for cmd, 4 for offset, 4 for length, 4 for garbage */
	FuDellDockMst *self = FU_DELL_DOCK_MST(device);
	FuDevice *proxy = fu_device_get_proxy(device);
	gint buffer_len = (data == NULL) ? 12 : length + 16;
	g_autofree guint8 *buffer = g_malloc0(buffer_len);
	guint32 tmp;

	g_return_val_if_fail(proxy != NULL, FALSE);

	/* command */
	tmp = (cmd | 0x80) << 16;
	memcpy(buffer, &tmp, 4);
	/* offset */
	memcpy(buffer + 4, &offset, 4);
	/* length */
	memcpy(buffer + 8, &length, 4);
	/* data */
	if (data != NULL)
		memcpy(buffer + 16, data, length);

	/* write the combined register stream */
	if (!fu_dell_dock_mst_write_register(proxy,
					     self->mst_rc_command_addr,
					     buffer,
					     buffer_len,
					     error))
		return FALSE;

	return fu_dell_dock_trigger_rc_command(device, error);
}

static MSTType
fu_dell_dock_mst_check_type(FuDevice *device)
{
	GPtrArray *instance_ids;
	const gchar *tmp = NULL;

	instance_ids = fu_device_get_instance_ids(device);
	for (guint i = 0; i < instance_ids->len; i++) {
		tmp = g_ptr_array_index(instance_ids, i);
		if (g_strcmp0(tmp, DELL_DOCK_VMM6210_INSTANCE_ID) == 0)
			return Cayenne_mst;
		else if (g_strcmp0(tmp, DELL_DOCK_VM5331_INSTANCE_ID) == 0)
			return Panamera_mst;
	}
	return Unknown;
}

static gboolean
fu_dell_dock_mst_check_offset(guint8 byte, guint8 offset)
{
	if ((byte & offset) != 0)
		return TRUE;
	return FALSE;
}

static gboolean
fu_d19_mst_check_fw(FuDevice *device, GError **error)
{
	FuDellDockMst *self = FU_DELL_DOCK_MST(device);
	g_autoptr(GBytes) bytes = NULL;
	const guint8 *data;
	gsize length = 4;

	if (!fu_dell_dock_mst_read_register(fu_device_get_proxy(device),
					    self->mst_core_mcu_bootloader_addr,
					    length,
					    &bytes,
					    error))
		return FALSE;
	data = g_bytes_get_data(bytes, &length);

	g_debug("MST: firmware check: %d", fu_dell_dock_mst_check_offset(data[0], 0x01));
	g_debug("MST: HDCP key check: %d", fu_dell_dock_mst_check_offset(data[0], 0x02));
	g_debug("MST: Config0  check: %d", fu_dell_dock_mst_check_offset(data[0], 0x04));
	g_debug("MST: Config1  check: %d", fu_dell_dock_mst_check_offset(data[0], 0x08));

	if (fu_dell_dock_mst_check_offset(data[0], 0xF0))
		g_debug("MST: running in bootloader");
	else
		g_debug("MST: running in firmware");
	g_debug("MST: Error code: %x", data[1]);
	g_debug("MST: GPIO boot strap record: %d", data[2]);
	g_debug("MST: Bootloader version number %x", data[3]);

	return TRUE;
}

static guint16
fu_dell_dock_mst_get_crc(guint8 type, guint32 length, const guint8 *payload_data)
{
	static const guint16 CRC16_table[] = {
	    0x0000, 0x8005, 0x800f, 0x000a, 0x801b, 0x001e, 0x0014, 0x8011, 0x8033, 0x0036, 0x003c,
	    0x8039, 0x0028, 0x802d, 0x8027, 0x0022, 0x8063, 0x0066, 0x006c, 0x8069, 0x0078, 0x807d,
	    0x8077, 0x0072, 0x0050, 0x8055, 0x805f, 0x005a, 0x804b, 0x004e, 0x0044, 0x8041, 0x80c3,
	    0x00c6, 0x00cc, 0x80c9, 0x00d8, 0x80dd, 0x80d7, 0x00d2, 0x00f0, 0x80f5, 0x80ff, 0x00fa,
	    0x80eb, 0x00ee, 0x00e4, 0x80e1, 0x00a0, 0x80a5, 0x80af, 0x00aa, 0x80bb, 0x00be, 0x00b4,
	    0x80b1, 0x8093, 0x0096, 0x009c, 0x8099, 0x0088, 0x808d, 0x8087, 0x0082, 0x8183, 0x0186,
	    0x018c, 0x8189, 0x0198, 0x819d, 0x8197, 0x0192, 0x01b0, 0x81b5, 0x81bf, 0x01ba, 0x81ab,
	    0x01ae, 0x01a4, 0x81a1, 0x01e0, 0x81e5, 0x81ef, 0x01ea, 0x81fb, 0x01fe, 0x01f4, 0x81f1,
	    0x81d3, 0x01d6, 0x01dc, 0x81d9, 0x01c8, 0x81cd, 0x81c7, 0x01c2, 0x0140, 0x8145, 0x814f,
	    0x014a, 0x815b, 0x015e, 0x0154, 0x8151, 0x8173, 0x0176, 0x017c, 0x8179, 0x0168, 0x816d,
	    0x8167, 0x0162, 0x8123, 0x0126, 0x012c, 0x8129, 0x0138, 0x813d, 0x8137, 0x0132, 0x0110,
	    0x8115, 0x811f, 0x011a, 0x810b, 0x010e, 0x0104, 0x8101, 0x8303, 0x0306, 0x030c, 0x8309,
	    0x0318, 0x831d, 0x8317, 0x0312, 0x0330, 0x8335, 0x833f, 0x033a, 0x832b, 0x032e, 0x0324,
	    0x8321, 0x0360, 0x8365, 0x836f, 0x036a, 0x837b, 0x037e, 0x0374, 0x8371, 0x8353, 0x0356,
	    0x035c, 0x8359, 0x0348, 0x834d, 0x8347, 0x0342, 0x03c0, 0x83c5, 0x83cf, 0x03ca, 0x83db,
	    0x03de, 0x03d4, 0x83d1, 0x83f3, 0x03f6, 0x03fc, 0x83f9, 0x03e8, 0x83ed, 0x83e7, 0x03e2,
	    0x83a3, 0x03a6, 0x03ac, 0x83a9, 0x03b8, 0x83bd, 0x83b7, 0x03b2, 0x0390, 0x8395, 0x839f,
	    0x039a, 0x838b, 0x038e, 0x0384, 0x8381, 0x0280, 0x8285, 0x828f, 0x028a, 0x829b, 0x029e,
	    0x0294, 0x8291, 0x82b3, 0x02b6, 0x02bc, 0x82b9, 0x02a8, 0x82ad, 0x82a7, 0x02a2, 0x82e3,
	    0x02e6, 0x02ec, 0x82e9, 0x02f8, 0x82fd, 0x82f7, 0x02f2, 0x02d0, 0x82d5, 0x82df, 0x02da,
	    0x82cb, 0x02ce, 0x02c4, 0x82c1, 0x8243, 0x0246, 0x024c, 0x8249, 0x0258, 0x825d, 0x8257,
	    0x0252, 0x0270, 0x8275, 0x827f, 0x027a, 0x826b, 0x026e, 0x0264, 0x8261, 0x0220, 0x8225,
	    0x822f, 0x022a, 0x823b, 0x023e, 0x0234, 0x8231, 0x8213, 0x0216, 0x021c, 0x8219, 0x0208,
	    0x820d, 0x8207, 0x0202};
	static const guint16 CRC8_table[] = {
	    0x00, 0xd5, 0x7f, 0xaa, 0xfe, 0x2b, 0x81, 0x54, 0x29, 0xfc, 0x56, 0x83, 0xd7, 0x02,
	    0xa8, 0x7d, 0x52, 0x87, 0x2d, 0xf8, 0xac, 0x79, 0xd3, 0x06, 0x7b, 0xae, 0x04, 0xd1,
	    0x85, 0x50, 0xfa, 0x2f, 0xa4, 0x71, 0xdb, 0x0e, 0x5a, 0x8f, 0x25, 0xf0, 0x8d, 0x58,
	    0xf2, 0x27, 0x73, 0xa6, 0x0c, 0xd9, 0xf6, 0x23, 0x89, 0x5c, 0x08, 0xdd, 0x77, 0xa2,
	    0xdf, 0x0a, 0xa0, 0x75, 0x21, 0xf4, 0x5e, 0x8b, 0x9d, 0x48, 0xe2, 0x37, 0x63, 0xb6,
	    0x1c, 0xc9, 0xb4, 0x61, 0xcb, 0x1e, 0x4a, 0x9f, 0x35, 0xe0, 0xcf, 0x1a, 0xb0, 0x65,
	    0x31, 0xe4, 0x4e, 0x9b, 0xe6, 0x33, 0x99, 0x4c, 0x18, 0xcd, 0x67, 0xb2, 0x39, 0xec,
	    0x46, 0x93, 0xc7, 0x12, 0xb8, 0x6d, 0x10, 0xc5, 0x6f, 0xba, 0xee, 0x3b, 0x91, 0x44,
	    0x6b, 0xbe, 0x14, 0xc1, 0x95, 0x40, 0xea, 0x3f, 0x42, 0x97, 0x3d, 0xe8, 0xbc, 0x69,
	    0xc3, 0x16, 0xef, 0x3a, 0x90, 0x45, 0x11, 0xc4, 0x6e, 0xbb, 0xc6, 0x13, 0xb9, 0x6c,
	    0x38, 0xed, 0x47, 0x92, 0xbd, 0x68, 0xc2, 0x17, 0x43, 0x96, 0x3c, 0xe9, 0x94, 0x41,
	    0xeb, 0x3e, 0x6a, 0xbf, 0x15, 0xc0, 0x4b, 0x9e, 0x34, 0xe1, 0xb5, 0x60, 0xca, 0x1f,
	    0x62, 0xb7, 0x1d, 0xc8, 0x9c, 0x49, 0xe3, 0x36, 0x19, 0xcc, 0x66, 0xb3, 0xe7, 0x32,
	    0x98, 0x4d, 0x30, 0xe5, 0x4f, 0x9a, 0xce, 0x1b, 0xb1, 0x64, 0x72, 0xa7, 0x0d, 0xd8,
	    0x8c, 0x59, 0xf3, 0x26, 0x5b, 0x8e, 0x24, 0xf1, 0xa5, 0x70, 0xda, 0x0f, 0x20, 0xf5,
	    0x5f, 0x8a, 0xde, 0x0b, 0xa1, 0x74, 0x09, 0xdc, 0x76, 0xa3, 0xf7, 0x22, 0x88, 0x5d,
	    0xd6, 0x03, 0xa9, 0x7c, 0x28, 0xfd, 0x57, 0x82, 0xff, 0x2a, 0x80, 0x55, 0x01, 0xd4,
	    0x7e, 0xab, 0x84, 0x51, 0xfb, 0x2e, 0x7a, 0xaf, 0x05, 0xd0, 0xad, 0x78, 0xd2, 0x07,
	    0x53, 0x86, 0x2c, 0xf9};
	guint8 val;
	guint16 crc = 0;
	const guint8 *message = payload_data;

	if (type == 8) {
		for (guint32 byte = 0; byte < length; ++byte) {
			val = (guint8)(message[byte] ^ crc);
			crc = CRC8_table[val];
		}
	} else {
		for (guint32 byte = 0; byte < length; ++byte) {
			val = (guint8)(message[byte] ^ (crc >> 8));
			crc = CRC16_table[val] ^ (crc << 8);
		}
	}

	return crc;
}

static gboolean
fu_dell_dock_mst_checksum_bank(FuDevice *device,
			       GBytes *blob_fw,
			       MSTBank bank,
			       gboolean *checksum,
			       GError **error)
{
	FuDellDockMst *self = FU_DELL_DOCK_MST(device);
	g_autoptr(GBytes) csum_bytes = NULL;
	const MSTBankAttributes *attribs = NULL;
	gsize length = 0;
	const guint8 *data = g_bytes_get_data(blob_fw, &length);
	guint32 payload_sum = 0;
	guint32 bank_sum = 0;

	g_return_val_if_fail(blob_fw != NULL, FALSE);
	g_return_val_if_fail(checksum != NULL, FALSE);

	if (!fu_dell_dock_mst_get_bank_attribs(bank, &attribs, error))
		return FALSE;

	/* bank is specified outside of payload */
	if (attribs->start + attribs->length > length) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Payload %u is bigger than bank %u",
			    attribs->start + attribs->length,
			    bank);
		return FALSE;
	}

	/* checksum the file */
	if (attribs->checksum_cmd == MST_CMD_CRC16_CHECKSUM)
		payload_sum =
		    fu_dell_dock_mst_get_crc(16, (attribs->length + attribs->start), data);
	else {
		for (guint i = attribs->start; i < attribs->length + attribs->start; i++) {
			payload_sum += data[i];
		}
	}
	g_debug("MST: Payload checksum: 0x%x", payload_sum);

	/* checksum the bank */
	if (!fu_dell_dock_mst_rc_command(device,
					 attribs->checksum_cmd,
					 attribs->length,
					 attribs->start,
					 NULL,
					 error)) {
		g_prefix_error(error, "Failed to checksum bank %u: ", bank);
		return FALSE;
	}
	/* read result from data register */
	if (!fu_dell_dock_mst_read_register(fu_device_get_proxy(device),
					    self->mst_rc_data_addr,
					    4,
					    &csum_bytes,
					    error))
		return FALSE;
	data = g_bytes_get_data(csum_bytes, NULL);
	bank_sum = GUINT32_FROM_LE(data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24);
	g_debug("MST: Bank %u checksum: 0x%x", bank, bank_sum);

	*checksum = (bank_sum == payload_sum);

	return TRUE;
}

static gboolean
fu_dell_dock_mst_erase_panamera_bank(FuDevice *device, MSTBank bank, GError **error)
{
	const MSTBankAttributes *attribs = NULL;
	guint32 sector;

	if (!fu_dell_dock_mst_get_bank_attribs(bank, &attribs, error))
		return FALSE;

	for (guint32 i = attribs->start; i < attribs->start + attribs->length; i += 0x10000) {
		sector = FLASH_SECTOR_ERASE_64K | (i / 0x10000);
		g_debug("MST: Erasing sector 0x%x", sector);
		if (!fu_dell_dock_mst_rc_command(device,
						 MST_CMD_ERASE_FLASH,
						 4,
						 0,
						 (guint8 *)&sector,
						 error)) {
			g_prefix_error(error, "Failed to erase sector 0x%x: ", sector);
			return FALSE;
		}
	}
	g_debug("MST: Waiting for flash clear to settle");
	g_usleep(5000000);

	return TRUE;
}

static gboolean
fu_dell_dock_mst_erase_cayenne(FuDevice *device, GError **error)
{
	guint8 data[4] = {0, 0x30, 0, 0};

	for (guint8 i = 0; i < 5; i++) {
		data[0] = i;
		if (!fu_dell_dock_mst_rc_command(device,
						 MST_CMD_ERASE_FLASH,
						 4,
						 0,
						 (guint8 *)&data,
						 error)) {
			g_prefix_error(error, "Failed to erase sector: %d", i);
			return FALSE;
		}
	}
	g_debug("MST: Waiting for flash clear to settle");
	g_usleep(5000000);

	return TRUE;
}

static gboolean
fu_dell_dock_write_flash_bank(FuDevice *device,
			      GBytes *blob_fw,
			      MSTBank bank,
			      FuProgress *progress,
			      GError **error)
{
	const MSTBankAttributes *attribs = NULL;
	gsize write_size = 32;
	guint end;
	const guint8 *data = g_bytes_get_data(blob_fw, NULL);

	g_return_val_if_fail(blob_fw != NULL, FALSE);

	if (!fu_dell_dock_mst_get_bank_attribs(bank, &attribs, error))
		return FALSE;
	end = attribs->start + attribs->length;

	g_debug("MST: Writing payload to bank %u", bank);
	for (guint i = attribs->start; i < end; i += write_size) {
		if (!fu_dell_dock_mst_rc_command(device,
						 MST_CMD_WRITE_FLASH,
						 write_size,
						 i,
						 data + i,
						 error)) {
			g_prefix_error(error,
				       "Failed to write bank %u payload offset 0x%x: ",
				       bank,
				       i);
			return FALSE;
		}
		fu_progress_set_percentage_full(progress, i - attribs->start, end - attribs->start);
	}

	return TRUE;
}

static gboolean
fu_dell_dock_mst_stop_esm(FuDevice *device, GError **error)
{
	g_autoptr(GBytes) quad_bytes = NULL;
	g_autoptr(GBytes) hdcp_bytes = NULL;
	guint32 payload = 0x21;
	gsize length = sizeof(guint32);
	const guint8 *data;
	guint8 data_out[sizeof(guint32)];

	/* disable ESM first */
	if (!fu_dell_dock_mst_rc_command(device,
					 MST_CMD_WRITE_MEMORY,
					 length,
					 PANAMERA_MST_RC_TRIGGER_ADDR,
					 (guint8 *)&payload,
					 error))
		return FALSE;

	/* waiting for ESM exit */
	g_usleep(200);

	/* disable QUAD mode */
	if (!fu_dell_dock_mst_rc_command(device,
					 MST_CMD_READ_MEMORY,
					 length,
					 PANAMERA_MST_REG_QUAD_DISABLE,
					 NULL,
					 error))
		return FALSE;

	if (!fu_dell_dock_mst_read_register(fu_device_get_proxy(device),
					    PANAMERA_MST_RC_DATA_ADDR,
					    length,
					    &quad_bytes,
					    error))
		return FALSE;

	data = g_bytes_get_data(quad_bytes, &length);
	memcpy(data_out, data, length);
	data_out[0] = 0x00;
	if (!fu_dell_dock_mst_rc_command(device,
					 MST_CMD_WRITE_MEMORY,
					 length,
					 PANAMERA_MST_REG_QUAD_DISABLE,
					 data_out,
					 error))
		return FALSE;

	/* disable HDCP2.2 */
	if (!fu_dell_dock_mst_rc_command(device,
					 MST_CMD_READ_MEMORY,
					 length,
					 PANAMERA_MST_REG_HDCP22_DISABLE,
					 NULL,
					 error))
		return FALSE;

	if (!fu_dell_dock_mst_read_register(fu_device_get_proxy(device),
					    PANAMERA_MST_RC_DATA_ADDR,
					    length,
					    &hdcp_bytes,
					    error))
		return FALSE;

	data = g_bytes_get_data(hdcp_bytes, &length);
	memcpy(data_out, data, length);
	data_out[0] = data[0] & (1 << 2);
	if (!fu_dell_dock_mst_rc_command(device,
					 MST_CMD_WRITE_MEMORY,
					 length,
					 PANAMERA_MST_REG_HDCP22_DISABLE,
					 data_out,
					 error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_dell_dock_mst_invalidate_bank(FuDevice *device, MSTBank bank_in_use, GError **error)
{
	const MSTBankAttributes *attribs;
	g_autoptr(GBytes) bytes = NULL;
	const guint8 *crc_tag;
	const guint8 *new_tag;
	guint32 crc_offset;
	guint retries = 2;

	if (!fu_dell_dock_mst_get_bank_attribs(bank_in_use, &attribs, error)) {
		g_prefix_error(error, "unable to invalidate bank: ");
		return FALSE;
	}
	/* we need to write 4 byte increments over I2C so this differs from DP aux */
	crc_offset = attribs->start + EEPROM_TAG_OFFSET + 12;

	/* Read CRC byte to flip */
	if (!fu_dell_dock_mst_rc_command(device, MST_CMD_READ_FLASH, 4, crc_offset, NULL, error)) {
		g_prefix_error(error, "failed to read tag from flash: ");
		return FALSE;
	}
	if (!fu_dell_dock_mst_read_register(fu_device_get_proxy(device),
					    PANAMERA_MST_RC_DATA_ADDR,
					    1,
					    &bytes,
					    error)) {
		return FALSE;
	}
	crc_tag = g_bytes_get_data(bytes, NULL);
	g_debug("CRC byte is currently 0x%x", crc_tag[3]);

	for (guint32 retries_cnt = 0;; retries_cnt++) {
		g_autoptr(GBytes) bytes_new = NULL;
		/* CRC8 is not 0xff, erase last 4k of bank# */
		if (crc_tag[3] != 0xff) {
			guint32 sector = FLASH_SECTOR_ERASE_4K +
					 (attribs->start + attribs->length - 0x1000) / 0x1000;
			g_debug("Erasing 4k from sector 0x%x invalidate bank %u",
				sector,
				bank_in_use);
			/* offset for last 4k of bank# */
			if (!fu_dell_dock_mst_rc_command(device,
							 MST_CMD_ERASE_FLASH,
							 4,
							 0,
							 (guint8 *)&sector,
							 error)) {
				g_prefix_error(error, "failed to erase sector 0x%x: ", sector);
				return FALSE;
			}
			/* CRC8 is 0xff, set it to 0x00 */
		} else {
			guint32 write = 0x00;
			g_debug("Writing 0x00 byte to 0x%x to invalidate bank %u",
				crc_offset,
				bank_in_use);
			if (!fu_dell_dock_mst_rc_command(device,
							 MST_CMD_WRITE_FLASH,
							 4,
							 crc_offset,
							 (guint8 *)&write,
							 error)) {
				g_prefix_error(error, "failed to clear CRC byte: ");
				return FALSE;
			}
		}
		/* re-read for comparison */
		if (!fu_dell_dock_mst_rc_command(device,
						 MST_CMD_READ_FLASH,
						 4,
						 crc_offset,
						 NULL,
						 error)) {
			g_prefix_error(error, "failed to read tag from flash: ");
			return FALSE;
		}
		if (!fu_dell_dock_mst_read_register(fu_device_get_proxy(device),
						    PANAMERA_MST_RC_DATA_ADDR,
						    4,
						    &bytes_new,
						    error)) {
			return FALSE;
		}
		new_tag = g_bytes_get_data(bytes_new, NULL);
		g_debug("CRC byte is currently 0x%x", new_tag[3]);

		/* tag successfully cleared */
		if ((new_tag[3] == 0xff && crc_tag[3] != 0xff) ||
		    (new_tag[3] == 0x00 && crc_tag[3] == 0xff)) {
			break;
		}
		if (retries_cnt > retries) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "set tag invalid fail (new 0x%x; old 0x%x)",
				    new_tag[3],
				    crc_tag[3]);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_dell_dock_mst_write_bank(FuDevice *device,
			    GBytes *fw,
			    guint8 bank,
			    FuProgress *progress,
			    GError **error)
{
	const guint retries = 2;
	for (guint i = 0; i < retries; i++) {
		gboolean checksum = FALSE;

		/* progress */
		fu_progress_set_id(progress, G_STRLOC);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 15, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 84, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 1, NULL);

		if (!fu_dell_dock_mst_erase_panamera_bank(device, bank, error))
			return FALSE;
		fu_progress_step_done(progress);

		if (!fu_dell_dock_write_flash_bank(device,
						   fw,
						   bank,
						   fu_progress_get_child(progress),
						   error))
			return FALSE;
		fu_progress_step_done(progress);

		if (!fu_dell_dock_mst_checksum_bank(device, fw, bank, &checksum, error))
			return FALSE;
		if (!checksum) {
			g_debug("MST: Failed to verify checksum on bank %u", bank);
			fu_progress_reset(progress);
			continue;
		}
		fu_progress_step_done(progress);

		g_debug("MST: Bank %u successfully flashed", bank);
		return TRUE;
	}

	/* failed after all our retries */
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to write to bank %u", bank);
	return FALSE;
}

static FuProgress *
fu_dell_dock_mst_set_local_progress(FuProgress *progress, guint steps)
{
	FuProgress *progress_local;
	progress_local = fu_progress_get_child(progress);
	fu_progress_set_id(progress_local, G_STRLOC);
	fu_progress_set_steps(progress_local, steps);

	return progress_local;
}

static gboolean
fu_dell_dock_mst_write_panamera(FuDevice *device,
				GBytes *fw,
				FwupdInstallFlags flags,
				FuProgress *progress,
				GError **error)
{
	gboolean checksum = FALSE;
	MSTBank bank_in_use = 0;
	guint8 order[2] = {ESM, Bank0};
	FuProgress *progress_local;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "stop-esm");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, NULL);
	/* determine the flash order */
	if (!fu_dell_dock_mst_query_active_bank(fu_device_get_proxy(device), &bank_in_use, error))
		return FALSE;

	if (bank_in_use == Bank0)
		order[1] = Bank1;
	/* ESM needs special handling during flash process*/
	if (!fu_dell_dock_mst_stop_esm(device, error))
		return FALSE;
	fu_progress_step_done(progress);

	progress_local = fu_dell_dock_mst_set_local_progress(progress, 2);
	/* Write each bank in order */
	for (guint phase = 0; phase < 2; phase++) {
		g_debug("MST: Checking bank %u", order[phase]);
		if (!fu_dell_dock_mst_checksum_bank(device, fw, order[phase], &checksum, error))
			return FALSE;
		if (checksum) {
			g_debug("MST: bank %u is already up to date", order[phase]);
			fu_progress_step_done(progress_local);
			continue;
		}
		g_debug("MST: bank %u needs to be updated", order[phase]);
		if (!fu_dell_dock_mst_write_bank(device,
						 fw,
						 order[phase],
						 fu_progress_get_child(progress_local),
						 error))
			return FALSE;
		fu_progress_step_done(progress_local);
	}
	/* invalidate the previous bank */
	if (!fu_dell_dock_mst_invalidate_bank(device, bank_in_use, error)) {
		g_prefix_error(error, "failed to invalidate bank %u: ", bank_in_use);
		return FALSE;
	}
	fu_progress_step_done(progress);

	return TRUE;
}

static gboolean
fu_dell_dock_mst_write_cayenne(FuDevice *device,
			       GBytes *fw,
			       FwupdInstallFlags flags,
			       FuProgress *progress,
			       GError **error)
{
	gboolean checksum = FALSE;
	guint retries = 2;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 3, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 97, NULL);

	for (guint i = 0; i < retries; i++) {
		if (!fu_dell_dock_mst_erase_cayenne(device, error))
			return FALSE;
		fu_progress_step_done(progress);
		if (!fu_dell_dock_write_flash_bank(device,
						   fw,
						   Cayenne,
						   fu_progress_get_child(progress),
						   error))
			return FALSE;
		if (!fu_dell_dock_mst_checksum_bank(device, fw, Cayenne, &checksum, error))
			return FALSE;
		fu_progress_step_done(progress);
		if (!checksum) {
			g_debug("MST: Failed to verify checksum");
			fu_progress_reset(progress);
			continue;
		}
		break;
	}
	/* failed after all our retries */
	if (!checksum) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to write to bank");
		return FALSE;
	}
	/* activate the FW */
	if (!fu_dell_dock_mst_rc_command(device,
					 MST_CMD_ACTIVATE_FW,
					 g_bytes_get_size(fw),
					 0x0,
					 NULL,
					 error)) {
		g_prefix_error(error, "Failed to activate FW: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_dock_mst_write_fw(FuDevice *device,
			  FuFirmware *firmware,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuDellDockMst *self = FU_DELL_DOCK_MST(device);
	const guint8 *data;
	g_autofree gchar *dynamic_version = NULL;
	g_autoptr(GBytes) fw = NULL;
	MSTType type;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);
	g_return_val_if_fail(fu_device_get_proxy(device) != NULL, FALSE);

	/* open the hub*/
	if (!fu_device_open(fu_device_get_proxy(device), error))
		return FALSE;

	/* open up access to controller bus */
	if (!fu_dell_dock_set_power(device, self->unlock_target, TRUE, error))
		return FALSE;

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	data = g_bytes_get_data(fw, NULL);

	dynamic_version = g_strdup_printf("%02x.%02x.%02x",
					  data[self->blob_major_offset],
					  data[self->blob_minor_offset],
					  data[self->blob_build_offset]);
	g_debug("writing MST firmware version %s", dynamic_version);

	/* enable remote control */
	if (!fu_dell_dock_mst_enable_remote_control(device, error))
		return FALSE;

	type = fu_dell_dock_mst_check_type(device);
	if (type == Panamera_mst) {
		if (!fu_dell_dock_mst_write_panamera(device, fw, flags, progress, error))
			return FALSE;
	} else if (type == Cayenne_mst) {
		if (!fu_dell_dock_mst_write_cayenne(device, fw, flags, progress, error))
			return FALSE;
	} else {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "Unknown mst found");
		return FALSE;
	}

	/* dock will reboot to re-read; this is to appease the daemon */
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, dynamic_version);

	/* disable remote control now */
	return fu_dell_dock_mst_disable_remote_control(device, error);
}

static gboolean
fu_dell_dock_mst_set_quirk_kv(FuDevice *device,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	FuDellDockMst *self = FU_DELL_DOCK_MST(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "DellDockUnlockTarget") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		self->unlock_target = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "DellDockBlobMajorOffset") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->blob_major_offset = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "DellDockBlobMinorOffset") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->blob_minor_offset = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "DellDockBlobBuildOffset") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->blob_build_offset = tmp;
		return TRUE;
	} else if (g_strcmp0(key, "DellDockInstallDurationI2C") == 0) {
		if (!fu_strtoull(value, &tmp, 0, 60 * 60 * 24, error))
			return FALSE;
		fu_device_set_install_duration(device, tmp);
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
	return FALSE;
}

static gboolean
fu_dell_dock_mst_setup(FuDevice *device, GError **error)
{
	FuDevice *parent;
	const gchar *version;

	/* sanity check that we can talk to MST */
	if (!fu_d19_mst_check_fw(device, error))
		return FALSE;

	/* set version from EC if we know it */
	parent = fu_device_get_parent(device);
	version = fu_dell_dock_ec_get_mst_version(parent);
	if (version != NULL) {
		fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
		fu_device_set_version(device, version);
	}

	return TRUE;
}

static gboolean
fu_dell_dock_mst_probe(FuDevice *device, GError **error)
{
	MSTType type;
	FuDellDockMst *self = FU_DELL_DOCK_MST(device);

	fu_device_set_logical_id(FU_DEVICE(device), "mst");

	/* confige mst register via instance id*/
	type = fu_dell_dock_mst_check_type(device);
	switch (type) {
	case Cayenne_mst:
		self->mst_rc_trigger_addr = CAYENNE_MST_RC_TRIGGER_ADDR;
		self->mst_rc_command_addr = CAYENNE_MST_RC_COMMAND_ADDR;
		self->mst_rc_data_addr = CAYENNE_MST_RC_DATA_ADDR;
		self->mst_core_mcu_bootloader_addr = CAYENNE_MST_CORE_MCU_BOOTLOADER_STS;
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
		return TRUE;
	case Panamera_mst:
		self->mst_rc_trigger_addr = PANAMERA_MST_RC_TRIGGER_ADDR;
		self->mst_rc_command_addr = PANAMERA_MST_RC_COMMAND_ADDR;
		self->mst_rc_data_addr = PANAMERA_MST_RC_DATA_ADDR;
		self->mst_core_mcu_bootloader_addr = PANAMERA_MST_CORE_MCU_BOOTLOADER_STS;
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
		return TRUE;
	case Unknown:
	default:
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "Unknown mst found");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_dell_dock_mst_open(FuDevice *device, GError **error)
{
	FuDellDockMst *self = FU_DELL_DOCK_MST(device);
	FuDevice *parent = fu_device_get_parent(device);

	g_return_val_if_fail(self->unlock_target != 0, FALSE);
	g_return_val_if_fail(parent != NULL, FALSE);

	if (fu_device_get_proxy(device) == NULL)
		fu_device_set_proxy(device, fu_device_get_proxy(parent));

	if (!fu_device_open(fu_device_get_proxy(device), error))
		return FALSE;

	/* open up access to controller bus */
	if (!fu_dell_dock_set_power(device, self->unlock_target, TRUE, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_dell_dock_mst_close(FuDevice *device, GError **error)
{
	FuDellDockMst *self = FU_DELL_DOCK_MST(device);

	/* close access to controller bus */
	if (!fu_dell_dock_set_power(device, self->unlock_target, FALSE, error))
		return FALSE;

	return fu_device_close(fu_device_get_proxy(device), error);
}

static void
fu_dell_dock_mst_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_dell_dock_mst_init(FuDellDockMst *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.synaptics.mst");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
}

static void
fu_dell_dock_mst_class_init(FuDellDockMstClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_dell_dock_mst_probe;
	klass_device->open = fu_dell_dock_mst_open;
	klass_device->close = fu_dell_dock_mst_close;
	klass_device->setup = fu_dell_dock_mst_setup;
	klass_device->probe = fu_dell_dock_mst_probe;
	klass_device->write_firmware = fu_dell_dock_mst_write_fw;
	klass_device->set_quirk_kv = fu_dell_dock_mst_set_quirk_kv;
	klass_device->set_progress = fu_dell_dock_mst_set_progress;
}

FuDellDockMst *
fu_dell_dock_mst_new(FuContext *ctx)
{
	FuDellDockMst *device = NULL;
	device = g_object_new(FU_TYPE_DELL_DOCK_MST, "context", ctx, NULL);
	return device;
}
