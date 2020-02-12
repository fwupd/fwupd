/*
 * Copyright (C) 2015-2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2018 Ryan Chang <ryan.chang@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>

#include "fu-synaptics-mst-common.h"
#include "fu-synaptics-mst-connection.h"
#include "fu-synaptics-mst-device.h"

#define FU_SYNAPTICS_MST_ID_CTRL_SIZE	0x1000
#define SYNAPTICS_UPDATE_ENUMERATE_TRIES 3

#define BIT(n)				(1 << (n))
#define FLASH_SECTOR_ERASE_4K		0x1000
#define FLASH_SECTOR_ERASE_32K		0x2000
#define FLASH_SECTOR_ERASE_64K		0x3000
#define EEPROM_TAG_OFFSET		0x1FFF0
#define EEPROM_BANK_OFFSET		0x20000
#define EEPROM_ESM_OFFSET		0x40000
#define ESM_CODE_SIZE			0x40000
#define PAYLOAD_SIZE_512K		0x80000
#define PAYLOAD_SIZE_64K		0x10000
#define MAX_RETRY_COUNTS		10
#define BLOCK_UNIT			64
#define BANKTAG_0			0
#define BANKTAG_1			1
#define CRC_8				8
#define CRC_16				16
#define REG_ESM_DISABLE			0x2000fc
#define REG_QUAD_DISABLE		0x200fc0
#define REG_HDCP22_DISABLE		0x200f90

#define FLASH_SETTLE_TIME		5000000	/* us */

struct _FuSynapticsMstDevice {
	FuUdevDevice		 parent_instance;
	gchar			*system_type;
	guint64			 write_block_size;
	FuSynapticsMstFamily	 family;
	FuSynapticsMstMode	 mode;
	guint8			 active_bank;
	guint8			 layer;
	guint16			 rad;		/* relative address */
	guint32			 board_id;
	guint16			 chip_id;
};

G_DEFINE_TYPE (FuSynapticsMstDevice, fu_synaptics_mst_device, FU_TYPE_UDEV_DEVICE)

static void
fu_synaptics_mst_device_finalize (GObject *object)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE (object);

	g_free (self->system_type);

	G_OBJECT_CLASS (fu_synaptics_mst_device_parent_class)->finalize (object);
}

static void
fu_synaptics_mst_device_init (FuSynapticsMstDevice *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "com.synaptics.mst");
	fu_device_set_vendor (FU_DEVICE (self), "Synaptics");
	fu_device_set_vendor_id (FU_DEVICE (self), "DRM_DP_AUX_DEV:0x06CB");
	fu_device_set_summary (FU_DEVICE (self), "Multi-Stream Transport Device");
	fu_device_add_icon (FU_DEVICE (self), "video-display");
	fu_udev_device_set_flags (FU_UDEV_DEVICE (self),
				  FU_UDEV_DEVICE_FLAG_OPEN_READ |
				  FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
				  FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT);
}

static void
fu_synaptics_mst_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE (device);
	if (self->mode != FU_SYNAPTICS_MST_MODE_UNKNOWN) {
		fu_common_string_append_kv (str, idt, "Mode",
					    fu_synaptics_mst_mode_to_string (self->mode));
	}
	if (self->family == FU_SYNAPTICS_MST_FAMILY_PANAMERA)
		fu_common_string_append_kx (str, idt, "ActiveBank", self->active_bank);
	fu_common_string_append_kx (str, idt, "Layer", self->layer);
	fu_common_string_append_kx (str, idt, "Rad", self->rad);
	if (self->board_id != 0x0)
		fu_common_string_append_ku (str, idt, "BoardId", self->board_id);
	if (self->chip_id != 0x0)
		fu_common_string_append_kx (str, idt, "ChipId", self->chip_id);
}

static gboolean
fu_synaptics_mst_device_enable_rc (FuSynapticsMstDevice *self, GError **error)
{
	g_autoptr(FuSynapticsMstConnection) connection = NULL;

	/* in test mode */
	if (fu_udev_device_get_dev (FU_UDEV_DEVICE (self)) == NULL)
		return TRUE;

	connection = fu_synaptics_mst_connection_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (self)),
						      self->layer, self->rad);
	return fu_synaptics_mst_connection_enable_rc (connection, error);
}

static gboolean
fu_synaptics_mst_device_disable_rc (FuSynapticsMstDevice *self, GError **error)
{
	g_autoptr(FuSynapticsMstConnection) connection = NULL;

	/* in test mode */
	if (fu_udev_device_get_dev (FU_UDEV_DEVICE (self)) == NULL)
		return TRUE;

	connection = fu_synaptics_mst_connection_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (self)),
						      self->layer, self->rad);
	return fu_synaptics_mst_connection_disable_rc (connection, error);
}

static gboolean
fu_synaptics_mst_device_probe (FuUdevDevice *device, GError **error)
{
	g_autofree gchar *logical_id = NULL;
	logical_id = g_path_get_basename (fu_udev_device_get_sysfs_path(device));
	fu_device_set_logical_id (FU_DEVICE (device), logical_id);
	if (!fu_udev_device_set_physical_id (device, "pci,drm_dp_aux_dev", error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_get_flash_checksum (FuSynapticsMstDevice *self,
					    guint32 length, guint32 offset,
					    guint32 *checksum, GError **error)
{
	g_autoptr(FuSynapticsMstConnection) connection = NULL;

	connection = fu_synaptics_mst_connection_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (self)),
						      self->layer, self->rad);
	if (!fu_synaptics_mst_connection_rc_special_get_command (connection,
								 UPDC_CAL_EEPROM_CHECKSUM,
								 length, offset,
								 NULL, 4,
								 (guint8 *)checksum,
								 error)) {
		g_prefix_error (error, "failed to get flash checksum: ");
		return FALSE;
	}

	return TRUE;
}

static guint16
fu_synaptics_mst_device_get_crc (guint16 crc, guint8 type, guint32 length, const guint8 *payload_data)
{
	static const guint16 CRC16_table[] = {
		0x0000, 0x8005, 0x800f, 0x000a, 0x801b, 0x001e, 0x0014, 0x8011, 0x8033, 0x0036, 0x003c, 0x8039, 0x0028, 0x802d, 0x8027, 0x0022,
		0x8063, 0x0066, 0x006c, 0x8069, 0x0078, 0x807d, 0x8077, 0x0072, 0x0050, 0x8055, 0x805f, 0x005a, 0x804b, 0x004e, 0x0044, 0x8041,
		0x80c3, 0x00c6, 0x00cc, 0x80c9, 0x00d8, 0x80dd, 0x80d7, 0x00d2, 0x00f0, 0x80f5, 0x80ff, 0x00fa, 0x80eb, 0x00ee, 0x00e4, 0x80e1,
		0x00a0, 0x80a5, 0x80af, 0x00aa, 0x80bb, 0x00be, 0x00b4, 0x80b1, 0x8093, 0x0096, 0x009c, 0x8099, 0x0088, 0x808d, 0x8087, 0x0082,
		0x8183, 0x0186, 0x018c, 0x8189, 0x0198, 0x819d, 0x8197, 0x0192, 0x01b0, 0x81b5, 0x81bf, 0x01ba, 0x81ab, 0x01ae, 0x01a4, 0x81a1,
		0x01e0, 0x81e5, 0x81ef, 0x01ea, 0x81fb, 0x01fe, 0x01f4, 0x81f1, 0x81d3, 0x01d6, 0x01dc, 0x81d9, 0x01c8, 0x81cd, 0x81c7, 0x01c2,
		0x0140, 0x8145, 0x814f, 0x014a, 0x815b, 0x015e, 0x0154, 0x8151, 0x8173, 0x0176, 0x017c, 0x8179, 0x0168, 0x816d, 0x8167, 0x0162,
		0x8123, 0x0126, 0x012c, 0x8129, 0x0138, 0x813d, 0x8137, 0x0132, 0x0110, 0x8115, 0x811f, 0x011a, 0x810b, 0x010e, 0x0104, 0x8101,
		0x8303, 0x0306, 0x030c, 0x8309, 0x0318, 0x831d, 0x8317, 0x0312, 0x0330, 0x8335, 0x833f, 0x033a, 0x832b, 0x032e, 0x0324, 0x8321,
		0x0360, 0x8365, 0x836f, 0x036a, 0x837b, 0x037e, 0x0374, 0x8371, 0x8353, 0x0356, 0x035c, 0x8359, 0x0348, 0x834d, 0x8347, 0x0342,
		0x03c0, 0x83c5, 0x83cf, 0x03ca, 0x83db, 0x03de, 0x03d4, 0x83d1, 0x83f3, 0x03f6, 0x03fc, 0x83f9, 0x03e8, 0x83ed, 0x83e7, 0x03e2,
		0x83a3, 0x03a6, 0x03ac, 0x83a9, 0x03b8, 0x83bd, 0x83b7, 0x03b2, 0x0390, 0x8395, 0x839f, 0x039a, 0x838b, 0x038e, 0x0384, 0x8381,
		0x0280, 0x8285, 0x828f, 0x028a, 0x829b, 0x029e, 0x0294, 0x8291, 0x82b3, 0x02b6, 0x02bc, 0x82b9, 0x02a8, 0x82ad, 0x82a7, 0x02a2,
		0x82e3, 0x02e6, 0x02ec, 0x82e9, 0x02f8, 0x82fd, 0x82f7, 0x02f2, 0x02d0, 0x82d5, 0x82df, 0x02da, 0x82cb, 0x02ce, 0x02c4, 0x82c1,
		0x8243, 0x0246, 0x024c, 0x8249, 0x0258, 0x825d, 0x8257, 0x0252, 0x0270, 0x8275, 0x827f, 0x027a, 0x826b, 0x026e, 0x0264, 0x8261,
		0x0220, 0x8225, 0x822f, 0x022a, 0x823b, 0x023e, 0x0234, 0x8231, 0x8213, 0x0216, 0x021c, 0x8219, 0x0208, 0x820d, 0x8207, 0x0202
	};
	static const guint16 CRC8_table[] = {
		0x00, 0xd5, 0x7f, 0xaa, 0xfe, 0x2b, 0x81, 0x54, 0x29, 0xfc, 0x56, 0x83, 0xd7, 0x02, 0xa8, 0x7d,
		0x52, 0x87, 0x2d, 0xf8, 0xac, 0x79, 0xd3, 0x06, 0x7b, 0xae, 0x04, 0xd1, 0x85, 0x50, 0xfa, 0x2f,
		0xa4, 0x71, 0xdb, 0x0e, 0x5a, 0x8f, 0x25, 0xf0, 0x8d, 0x58, 0xf2, 0x27, 0x73, 0xa6, 0x0c, 0xd9,
		0xf6, 0x23, 0x89, 0x5c, 0x08, 0xdd, 0x77, 0xa2, 0xdf, 0x0a, 0xa0, 0x75, 0x21, 0xf4, 0x5e, 0x8b,
		0x9d, 0x48, 0xe2, 0x37, 0x63, 0xb6, 0x1c, 0xc9, 0xb4, 0x61, 0xcb, 0x1e, 0x4a, 0x9f, 0x35, 0xe0,
		0xcf, 0x1a, 0xb0, 0x65, 0x31, 0xe4, 0x4e, 0x9b, 0xe6, 0x33, 0x99, 0x4c, 0x18, 0xcd, 0x67, 0xb2,
		0x39, 0xec, 0x46, 0x93, 0xc7, 0x12, 0xb8, 0x6d, 0x10, 0xc5, 0x6f, 0xba, 0xee, 0x3b, 0x91, 0x44,
		0x6b, 0xbe, 0x14, 0xc1, 0x95, 0x40, 0xea, 0x3f, 0x42, 0x97, 0x3d, 0xe8, 0xbc, 0x69, 0xc3, 0x16,
		0xef, 0x3a, 0x90, 0x45, 0x11, 0xc4, 0x6e, 0xbb, 0xc6, 0x13, 0xb9, 0x6c, 0x38, 0xed, 0x47, 0x92,
		0xbd, 0x68, 0xc2, 0x17, 0x43, 0x96, 0x3c, 0xe9, 0x94, 0x41, 0xeb, 0x3e, 0x6a, 0xbf, 0x15, 0xc0,
		0x4b, 0x9e, 0x34, 0xe1, 0xb5, 0x60, 0xca, 0x1f, 0x62, 0xb7, 0x1d, 0xc8, 0x9c, 0x49, 0xe3, 0x36,
		0x19, 0xcc, 0x66, 0xb3, 0xe7, 0x32, 0x98, 0x4d, 0x30, 0xe5, 0x4f, 0x9a, 0xce, 0x1b, 0xb1, 0x64,
		0x72, 0xa7, 0x0d, 0xd8, 0x8c, 0x59, 0xf3, 0x26, 0x5b, 0x8e, 0x24, 0xf1, 0xa5, 0x70, 0xda, 0x0f,
		0x20, 0xf5, 0x5f, 0x8a, 0xde, 0x0b, 0xa1, 0x74, 0x09, 0xdc, 0x76, 0xa3, 0xf7, 0x22, 0x88, 0x5d,
		0xd6, 0x03, 0xa9, 0x7c, 0x28, 0xfd, 0x57, 0x82, 0xff, 0x2a, 0x80, 0x55, 0x01, 0xd4, 0x7e, 0xab,
		0x84, 0x51, 0xfb, 0x2e, 0x7a, 0xaf, 0x05, 0xd0, 0xad, 0x78, 0xd2, 0x07, 0x53, 0x86, 0x2c, 0xf9
	};
	guint8 val;
	guint16 remainder = (guint16) crc;
	const guint8 *message = payload_data;

	if (type == CRC_8) {
		for (guint32 byte = 0; byte < length; ++byte) {
			val = (guint8)(message[byte] ^ remainder);
			remainder = CRC8_table[val];
		}
	} else {
		for (guint32 byte = 0; byte < length; ++byte) {
			val = (guint8)(message[byte] ^ (remainder >> 8));
			remainder = CRC16_table[val] ^ (remainder << 8);
		}
	}

	return remainder;
}

static gboolean
fu_synaptics_mst_device_set_flash_sector_erase (FuSynapticsMstDevice *self,
					    guint16 rc_cmd,
					    guint16 offset,
					    GError **error)
{
	guint16 us_data;
	g_autoptr(FuSynapticsMstConnection) connection = NULL;

	connection = fu_synaptics_mst_connection_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (self)),
						      self->layer, self->rad);
	/* Need to add Wp control ? */
	us_data = rc_cmd + offset;

	if (!fu_synaptics_mst_connection_rc_set_command (connection,
							 UPDC_FLASH_ERASE,
							 2, 0, (guint8 *)&us_data,
							 error)) {
		g_prefix_error (error, "can't sector erase flash at offset %x",
				offset);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_synaptics_mst_device_update_esm (FuSynapticsMstDevice *self,
				    const guint8 *payload_data,
				    GError **error)
{
	guint32 checksum = 0;
	guint32 esm_sz = ESM_CODE_SIZE;
	guint32 flash_checksum = 0;
	guint32 unit_sz = BLOCK_UNIT;
	guint32 write_loops = 0;
	g_autoptr(FuSynapticsMstConnection) connection = NULL;

	connection = fu_synaptics_mst_connection_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (self)),
						      self->layer, self->rad);

	for (guint32 i = 0; i < esm_sz; i++)
		checksum += *(payload_data + EEPROM_ESM_OFFSET +i);
	if (!fu_synaptics_mst_device_get_flash_checksum (self,
						    esm_sz,
						    EEPROM_ESM_OFFSET,
						    &flash_checksum, error)) {
		return FALSE;
	}

	/* ESM checksum same */
	if (checksum == flash_checksum) {
		g_debug ("ESM checksum already matches");
		return TRUE;
	}
	g_debug ("ESM checksum %x doesn't match expected %x", flash_checksum, checksum);

	/* update ESM firmware */
	write_loops = esm_sz / unit_sz;
	for (guint retries_cnt = 0; ; retries_cnt++) {
		guint32 write_idx = 0;
		guint32 write_offset = EEPROM_ESM_OFFSET;
		const guint8 *esm_code_ptr = &payload_data[EEPROM_ESM_OFFSET];

		/* erase ESM firmware; erase failure is fatal */
		for (guint32 j = 0; j < 4; j++)	{
			if (!fu_synaptics_mst_device_set_flash_sector_erase (self,
									     FLASH_SECTOR_ERASE_64K,
									     j + 4,
									     error)) {
				g_prefix_error (error, "failed to erase sector %u: ", j);
				return FALSE;
			}
		}

		g_debug ("Waiting for flash clear to settle");
		g_usleep (FLASH_SETTLE_TIME);

		/* write firmware */
		for (guint32 i = 0; i < write_loops; i++) {
			g_autoptr(GError) error_local = NULL;
			if (!fu_synaptics_mst_connection_rc_set_command (connection,
									 UPDC_WRITE_TO_EEPROM,
									 unit_sz,
									 write_offset,
									 esm_code_ptr + write_idx,
									 &error_local)) {
				g_warning ("failed to write ESM: %s", error_local->message);
				break;
			}
			write_offset += unit_sz;
			write_idx += unit_sz;
			fu_device_set_progress_full (FU_DEVICE (self),
						     (goffset) i * 100,
						     (goffset) (write_loops -1) * 100);
		}

		/* check ESM checksum */
		checksum = 0;
		flash_checksum = 0;
		for (guint32 i = 0; i < esm_sz; i++)
			checksum += *(payload_data + EEPROM_ESM_OFFSET +i);
		if (!fu_synaptics_mst_device_get_flash_checksum (self,
								 esm_sz,
								 EEPROM_ESM_OFFSET,
								 &flash_checksum,
								 error))
			return FALSE;

		/* ESM update done */
		if (checksum == flash_checksum)
			break;
		g_debug ("attempt %u: ESM checksum %x didn't match %x", retries_cnt, flash_checksum, checksum);

		/* abort */
		if (retries_cnt > MAX_RETRY_COUNTS) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "checksum did not match after %u tries", retries_cnt);
			return FALSE;
		}
	}
	g_debug ("ESM successfully written");

	return TRUE;
}

static gboolean
fu_synaptics_mst_device_update_tesla_leaf_firmware (FuSynapticsMstDevice *self,
						    guint32 payload_len,
						    const guint8 *payload_data,
						    GError **error)
{
	g_autoptr(FuSynapticsMstConnection) connection = NULL;
	guint32 data_to_write = 0;
	guint32 offset = 0;
	guint32 write_loops = 0;

	write_loops = (payload_len / BLOCK_UNIT);
	data_to_write = payload_len;

	if (payload_len % BLOCK_UNIT)
		write_loops++;

	connection = fu_synaptics_mst_connection_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (self)),
						      self->layer, self->rad);
	for (guint32 retries_cnt = 0; ; retries_cnt++) {
		guint32 checksum = 0;
		guint32 flash_checksum = 0;

		if (!fu_synaptics_mst_device_set_flash_sector_erase (self, 0xffff, 0, error))
			return FALSE;
		g_debug ("Waiting for flash clear to settle");
		g_usleep (FLASH_SETTLE_TIME);

		for (guint32 i = 0; i < write_loops; i++) {
			g_autoptr(GError) error_local = NULL;
			guint8 length = BLOCK_UNIT;

			if (data_to_write < BLOCK_UNIT)
				length = data_to_write;
			if (!fu_synaptics_mst_connection_rc_set_command (connection,
									 UPDC_WRITE_TO_EEPROM,
									 length, offset,
									 payload_data + offset,
									 &error_local)) {
				g_warning ("Failed to write flash offset 0x%04x: %s, retrying",
					   offset, error_local->message);
				/* repeat once */
				if (!fu_synaptics_mst_connection_rc_set_command (connection,
										 UPDC_WRITE_TO_EEPROM,
										 length, offset,
										 payload_data + offset,
										 error)) {
					g_prefix_error (error, "can't write flash offset 0x%04x: ",
							offset);
					return FALSE;
				}
			}
			offset += length;
			data_to_write -= length;
			fu_device_set_progress_full (FU_DEVICE (self),
						     (goffset) i * 100,
						     (goffset) (write_loops -1) * 100);
		}

		/* check data just written */
		for (guint32 i = 0; i < payload_len; i++)
			checksum += *(payload_data + i);

		if (!fu_synaptics_mst_device_get_flash_checksum (self,
								 payload_len,
								 0,
								 &flash_checksum,
								 error))
			return FALSE;
		if (checksum == flash_checksum)
			break;
		g_debug ("attempt %u: checksum %x didn't match %x", retries_cnt, flash_checksum, checksum);

		if (retries_cnt > MAX_RETRY_COUNTS) {
			g_set_error (error,
					G_IO_ERROR,
					G_IO_ERROR_INVALID_DATA,
					"checksum %x mismatched %x",
					flash_checksum,
					checksum);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_synaptics_mst_device_get_active_bank_panamera (FuSynapticsMstDevice *self, GError **error)
{
	g_autoptr(FuSynapticsMstConnection) connection = NULL;
	guint32 buf[16];

	/* get used bank */
	connection = fu_synaptics_mst_connection_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (self)),
						     self->layer, self->rad);
	if (!fu_synaptics_mst_connection_rc_get_command (connection,
							 UPDC_READ_FROM_MEMORY,
							 ((sizeof(buf)/sizeof(buf[0]))*4),
							 (gint) 0x20010c, (guint8*) buf,
							 error)) {
		g_prefix_error (error, "get active bank failed: ");
		return FALSE;
	}
	if ((buf[0] & BIT(7)) || (buf[0] & BIT(30)))
		self->active_bank = BANKTAG_1;
	else
		self->active_bank = BANKTAG_0;
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_update_panamera_firmware (FuSynapticsMstDevice *self,
						  guint32 payload_len,
						  const guint8 *payload_data,
						  GError **error)
{

	guint16 crc_tmp = 0;
	guint32 fw_size;
	guint32 unit_sz = BLOCK_UNIT;
	guint32 write_loops = 0;
	guint8 bank_to_update = BANKTAG_1;
	guint8 readBuf[256];
	guint8 tagData[16];
	struct tm *pTM;
	time_t timeptr;
	g_autoptr(FuSynapticsMstConnection) connection = NULL;

	/* get used bank */
	if (!fu_synaptics_mst_device_get_active_bank_panamera (self, error))
		return FALSE;
	if (self->active_bank == BANKTAG_1)
		bank_to_update = BANKTAG_0;
	g_debug ("bank to update:%x", bank_to_update);

	/* get firmware size */
	fw_size =  0x410 + (*(payload_data + 0x400) << 24)
			 + (*(payload_data + 0x401) << 16)
			 + (*(payload_data + 0x402) << 8)
			 + (*(payload_data + 0x403));

	/* Current max firmware size is 104K */
	if (fw_size < payload_len)
		fw_size = 104 * 1024;
	g_debug ("Calculated fw size as %u", fw_size);

	/* Update firmware */
	write_loops = fw_size / unit_sz;
	if (fw_size % unit_sz)
		write_loops++;

	for (guint32 retries_cnt = 0; ; retries_cnt++) {
		guint32 checksum = 0;
		guint32 erase_offset;
		guint32 flash_checksum = 0;
		guint32 write_idx;
		guint32 write_offset;

		/* erase storage */
		erase_offset = bank_to_update * 2;
		if (!fu_synaptics_mst_device_set_flash_sector_erase (self,
								     FLASH_SECTOR_ERASE_64K, erase_offset++, error))
			return FALSE;
		if (!fu_synaptics_mst_device_set_flash_sector_erase (self,
								     FLASH_SECTOR_ERASE_64K, erase_offset, error))
			return FALSE;
		g_debug ("Waiting for flash clear to settle");
		g_usleep (FLASH_SETTLE_TIME);

		/* write */
		write_idx = 0;
		write_offset = EEPROM_BANK_OFFSET * bank_to_update;
		connection = fu_synaptics_mst_connection_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (self)),
							     self->layer, self->rad);
		for (guint32 i = 0; i < write_loops ; i++ ) {
			g_autoptr(GError) error_local = NULL;
			if (!fu_synaptics_mst_connection_rc_set_command (connection,
									 UPDC_WRITE_TO_EEPROM,
									 unit_sz,
									 write_offset,
									 payload_data + write_idx,
									 &error_local)) {
				g_warning ("Write failed: %s, retrying", error_local->message);
				/* repeat once */
				if (!fu_synaptics_mst_connection_rc_set_command (connection,
									 UPDC_WRITE_TO_EEPROM,
									 unit_sz,
									 write_offset,
									 payload_data + write_idx,
									 error)) {
					g_prefix_error (error, "firmware write failed: ");
					return FALSE;
				}
			}

			write_offset += unit_sz;
			write_idx += unit_sz;
			fu_device_set_progress_full (FU_DEVICE (self),
						     (goffset) i * 100,
						     (goffset) (write_loops -1) * 100);
		}

		/* verify CRC */
		checksum = fu_synaptics_mst_device_get_crc (0, 16, fw_size, payload_data );
		for (guint32 i = 0; i < 4; i++) {
			g_usleep (1000);	/* wait crc calculation */
			if (!fu_synaptics_mst_connection_rc_special_get_command (connection,
										 UPDC_CAL_EEPROM_CHECK_CRC16,
										 fw_size, (EEPROM_BANK_OFFSET * bank_to_update),
										 NULL, 4, (guint8 *)(&flash_checksum),
										 error)) {
				g_prefix_error (error, "Failed to get flash checksum: ");
				return FALSE;
			}
		}
		if (checksum == flash_checksum)
			break;
		if (retries_cnt > MAX_RETRY_COUNTS) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "firmware update fail");
			return FALSE;
		}
		g_usleep (2000);
	}

	/* set tag valid */
	time (&timeptr);
	pTM = localtime (&timeptr);
	memset (tagData, 0, sizeof (tagData));
	memset (readBuf, 0, sizeof (readBuf));

	tagData[1] = pTM->tm_mon + 1;
	tagData[2] = pTM->tm_mday;
	tagData[3] = pTM->tm_year + 1900 - 2000;
	crc_tmp = fu_synaptics_mst_device_get_crc (0, 16, fw_size, payload_data);
	tagData[0] = bank_to_update;
	tagData[4] = (crc_tmp >> 8) & 0xff;
	tagData[5] = crc_tmp & 0xff;
	tagData[15] = (guint8) fu_synaptics_mst_device_get_crc (0, 8, 15, tagData);
	g_debug ("tag date %x %x %x crc %x %x %x %x", tagData[1], tagData[2], tagData[3], tagData[0], tagData[4], tagData[5], tagData[15]);

	for (guint32 retries_cnt = 0; ; retries_cnt++) {
		gboolean match = TRUE;
		if (!fu_synaptics_mst_connection_rc_set_command (connection,
								 UPDC_WRITE_TO_EEPROM,
								 16,
								 (EEPROM_BANK_OFFSET * bank_to_update + EEPROM_TAG_OFFSET),
								 tagData,
								 error)) {
			g_prefix_error (error, "failed to write tag: ");
			return FALSE;
		}
		g_usleep (200);
		if (!fu_synaptics_mst_connection_rc_get_command (connection,
								 UPDC_READ_FROM_EEPROM,
								 16,
								 (EEPROM_BANK_OFFSET * bank_to_update + EEPROM_TAG_OFFSET),
								 readBuf,
								 error)) {
			g_prefix_error (error, "failed to read tag: ");
			return FALSE;
		}
		for (guint32 i = 0; i < 16; i++){
			if (readBuf[i] != tagData[i]){
				match = FALSE;
				break;
			}
		}
		if (match)
			break;
		if (retries_cnt > MAX_RETRY_COUNTS) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "set tag valid fail");
			return FALSE;
		}
	}

	/* set tag invalid*/
	if (!fu_synaptics_mst_connection_rc_get_command (connection,
							 UPDC_READ_FROM_EEPROM, 1,
							 (EEPROM_BANK_OFFSET * self->active_bank + EEPROM_TAG_OFFSET + 15),
							 tagData,
							 error)) {
		g_prefix_error (error, "failed to read tag from flash: ");
		return FALSE;
	}

	for (guint32 retries_cnt = 0; ; retries_cnt++) {
		/* CRC8 is not 0xff, erase last 4k of bank# */
		if (tagData[0] != 0xff)	{
			guint32 erase_offset;
			/* offset for last 4k of bank# */
			erase_offset = (EEPROM_BANK_OFFSET * self->active_bank + EEPROM_BANK_OFFSET - 0x1000) / 0x1000;
			if (!fu_synaptics_mst_device_set_flash_sector_erase (self,
									     FLASH_SECTOR_ERASE_4K,
									     erase_offset,
									     error))
				return FALSE;
		/* CRC8 is 0xff, set it to 0x00 */
		} else {
			tagData[1] = 0x00;
			if (!fu_synaptics_mst_connection_rc_set_command (connection,
									 UPDC_WRITE_TO_EEPROM, 1,
									 (EEPROM_BANK_OFFSET * self->active_bank + EEPROM_TAG_OFFSET + 15),
									 &tagData[1],
									 error)) {
				g_prefix_error (error, "failed to clear CRC: ");
				return FALSE;
			}
		}
		if (!fu_synaptics_mst_connection_rc_get_command (connection,
								 UPDC_READ_FROM_EEPROM, 1,
								 (EEPROM_BANK_OFFSET * self->active_bank + EEPROM_TAG_OFFSET + 15),
								 readBuf,
								 error)) {
			g_prefix_error (error, "failed to read CRC from flash: ");
			return FALSE;
		}
		if ((readBuf[0] == 0xff && tagData[0] != 0xff) ||
		    (readBuf[0] == 0x00 && tagData[0] == 0xff)) {
			break;
		}
		if (retries_cnt > MAX_RETRY_COUNTS) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "set tag invalid fail");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_synaptics_mst_device_panamera_prepare_write (FuSynapticsMstDevice *self, GError **error)
{
	guint32 buf[4] = {0};
	g_autoptr(FuSynapticsMstConnection) connection = NULL;

	/* Need to detect flash mode and ESM first ? */
	/* disable flash Quad mode and ESM/HDCP2.2*/
	connection = fu_synaptics_mst_connection_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (self)),
						      self->layer, self->rad);

	/* disable ESM first */
	buf[0] = 0x21;
	if (!fu_synaptics_mst_connection_rc_set_command (connection,
							 UPDC_WRITE_TO_MEMORY,
							 4, (gint)REG_ESM_DISABLE, (guint8*)buf,
							 error)) {
		g_prefix_error (error, "ESM disable failed: ");
		return FALSE;
	}

	/* wait for ESM exit */
	g_usleep (200);

	/* disable QUAD mode */
	if (!fu_synaptics_mst_connection_rc_get_command (connection,
							 UPDC_READ_FROM_MEMORY,
							 ((sizeof(buf)/sizeof(buf[0]))*4),
							 (gint)REG_QUAD_DISABLE, (guint8*)buf,
							 error)) {
		g_prefix_error (error, "quad query failed: ");
		return FALSE;
	}

	buf[0] = 0x00;
	if (!fu_synaptics_mst_connection_rc_set_command (connection,
							 UPDC_WRITE_TO_MEMORY,
							 4, (gint)REG_QUAD_DISABLE, (guint8*)buf,
							 error)) {
		g_prefix_error (error, "quad disable failed: ");
		return FALSE;
	}

	/* disable HDCP2.2 */
	if (!fu_synaptics_mst_connection_rc_get_command (connection,
							 UPDC_READ_FROM_MEMORY,
							 4, (gint)REG_HDCP22_DISABLE, (guint8*)buf,
							 error)) {
		g_prefix_error (error, "HDCP query failed: ");
		return FALSE;
	}

	buf[0] = buf[0] & (~BIT(2));
	if (!fu_synaptics_mst_connection_rc_set_command (connection,
							 UPDC_WRITE_TO_MEMORY,
							 4, (gint)REG_HDCP22_DISABLE, (guint8*)buf,
							 error)) {
		g_prefix_error (error, "HDCP disable failed: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_synaptics_mst_device_restart (FuSynapticsMstDevice *self, GError **error)
{
	g_autoptr(FuSynapticsMstConnection) connection = NULL;
	guint8 buf[4] = {0xF5, 0, 0 ,0};
	g_autoptr(GError) error_local = NULL;

	/* issue the reboot command, ignore return code (triggers before returning) */
	connection = fu_synaptics_mst_connection_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (self)),
						      self->layer, self->rad);
	if (!fu_synaptics_mst_connection_rc_set_command (connection,
							 UPDC_WRITE_TO_MEMORY,
							 4, (gint) 0x2000FC, (guint8*) &buf,
							 &error_local))
		g_debug ("failed to restart: %s", error_local->message);

	return TRUE;
}

static FuFirmware *
fu_synaptics_mst_device_prepare_firmware (FuDevice *device,
					  GBytes *fw,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE (device);

	/* check firmware and board ID match */
	if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0 &&
	    !fu_device_has_custom_flag (device, "ignore-board-id")) {
		const guint8 *buf;
		gsize len;
		guint16 board_id;

		buf = g_bytes_get_data (fw, &len);
		board_id = fu_common_read_uint16 (buf + ADDR_CUSTOMER_ID, G_BIG_ENDIAN);
		if (board_id != self->board_id) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "board ID mismatch, got 0x%04x, expected 0x%04x",
				     board_id, self->board_id);
			return NULL;
		}
	}
	return fu_firmware_new_from_bytes (fw);
}

static gboolean
fu_synaptics_mst_device_write_firmware (FuDevice *device,
				        FuFirmware *firmware,
				        FwupdInstallFlags flags,
				        GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE (device);
	g_autoptr(GBytes) fw = NULL;
	const guint8 *payload_data;
	gsize payload_len;
	g_autoptr(FuDeviceLocker) locker = NULL;

	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;
	payload_data = g_bytes_get_data (fw, &payload_len);

	/* enable remote control and disable on exit */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_device_has_custom_flag (device, "skip-restart")) {
		locker = fu_device_locker_new_full (self,
						(FuDeviceLockerFunc) fu_synaptics_mst_device_enable_rc,
						(FuDeviceLockerFunc) fu_synaptics_mst_device_restart,
						error);
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		fu_device_set_remove_delay (FU_DEVICE (self), 10000); /* a long time */
	} else {
		locker = fu_device_locker_new_full (self,
						(FuDeviceLockerFunc) fu_synaptics_mst_device_enable_rc,
						(FuDeviceLockerFunc) fu_synaptics_mst_device_disable_rc,
						error);
	}
	if (locker == NULL)
		return FALSE;

	/* update firmware */
	if (self->family == FU_SYNAPTICS_MST_FAMILY_PANAMERA) {
		if (!fu_synaptics_mst_device_panamera_prepare_write (self, error)) {
			g_prefix_error (error, "Failed to prepare for write: ");
			return FALSE;
		}
		if (!fu_synaptics_mst_device_update_esm (self,
							 payload_data,
							 error)) {
			g_prefix_error (error, "ESM update failed: ");
			return FALSE;
		}
		if (!fu_synaptics_mst_device_update_panamera_firmware (self,
								       payload_len,
								       payload_data,
								       error)) {
			g_prefix_error (error, "Firmware update failed: ");
			return FALSE;
		}
	} else {
		if (!fu_synaptics_mst_device_update_tesla_leaf_firmware (self,
									 payload_len,
									 payload_data,
									 error)) {
			g_prefix_error (error, "Firmware update failed: ");
			return FALSE;
		}
	}
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	return TRUE;
}

FuSynapticsMstDevice *
fu_synaptics_mst_device_new (FuUdevDevice *device)
{
	FuSynapticsMstDevice *self = g_object_new (FU_TYPE_SYNAPTICS_MST_DEVICE, NULL);
	fu_device_incorporate (FU_DEVICE (self), FU_DEVICE (device));
	return self;
}

static gboolean
fu_synaptics_mst_device_read_board_id (FuSynapticsMstDevice *self,
				       FuSynapticsMstConnection *connection,
				       guint8 *byte,
				       GError **error)
{
	/* in test mode we need to open a different file node instead */
	if (fu_udev_device_get_dev (FU_UDEV_DEVICE (self)) == NULL) {
		g_autofree gchar *filename = NULL;
		g_autofree gchar *dirname = NULL;
		gint fd;
		dirname = g_path_get_dirname (fu_udev_device_get_device_file (FU_UDEV_DEVICE (self)));
		filename = g_strdup_printf ("%s/remote/%s_eeprom",
					    dirname,
					    fu_device_get_logical_id (FU_DEVICE (self)));
		if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_FOUND,
				     "no device exists %s",
				     filename);
			return FALSE;
		}
		fd = open (filename, O_RDONLY);
		if (fd == -1) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_PERMISSION_DENIED,
				     "cannot open device %s",
				     filename);
			return FALSE;
		}
		if (read (fd, byte, 2) != 2) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "error reading EEPROM file %s",
				     filename);
			close (fd);
			return FALSE;
		}
		close (fd);
		return TRUE;
	}

	/* get board ID via MCU address 0x170E instead of flash access due to HDCP2.2 running */
	if (!fu_synaptics_mst_connection_rc_get_command (connection,
							 UPDC_READ_FROM_MEMORY,
							 2,
							 (gint)ADDR_MEMORY_CUSTOMER_ID, byte,
							 error)) {
		g_prefix_error (error, "Memory query failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_synaptics_mst_device_scan_cascade (FuSynapticsMstDevice *self, guint8 layer, GError **error)
{
	/* in test mode we skip this */
	if (fu_udev_device_get_dev (FU_UDEV_DEVICE (self)) == NULL)
		return TRUE;

	/* test each relative address in this layer */
	for (guint16 rad = 0; rad <= 2; rad++) {
		guint8 byte[4];
		g_autoptr(FuDeviceLocker) locker = NULL;
		g_autoptr(FuSynapticsMstConnection) connection = NULL;
		g_autoptr(FuSynapticsMstDevice) device_tmp = NULL;
		g_autoptr(GError) error_local = NULL;

		/* enable remote control and disable on exit */
		device_tmp = fu_synaptics_mst_device_new (FU_UDEV_DEVICE (self));
		device_tmp->layer = layer;
		device_tmp->rad = rad;
		locker = fu_device_locker_new_full (device_tmp,
						    (FuDeviceLockerFunc) fu_synaptics_mst_device_enable_rc,
						    (FuDeviceLockerFunc) fu_synaptics_mst_device_disable_rc,
						    &error_local);
		if (locker == NULL) {
			g_debug ("no cascade device found: %s", error_local->message);
			continue;
		}
		connection = fu_synaptics_mst_connection_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (self)),
							      layer + 1, rad);
		if (!fu_synaptics_mst_connection_read (connection, REG_RC_CAP, byte, 1, &error_local)) {
			g_debug ("no valid cascade device: %s", error_local->message);
			continue;
		}

		/* check recursively for more devices */
		g_clear_object (&locker);
		self->mode = FU_SYNAPTICS_MST_MODE_REMOTE;
		self->layer = layer + 1;
		self->rad = rad;
		if (!fu_synaptics_mst_device_scan_cascade (self, layer + 1, error))
			return FALSE;
	}
	return TRUE;
}

void
fu_synaptics_mst_device_set_system_type (FuSynapticsMstDevice *self, const gchar *system_type)
{
	g_return_if_fail (FU_IS_SYNAPTICS_MST_DEVICE (self));
	self->system_type = g_strdup (system_type);
}

static gboolean
fu_synaptics_mst_device_rescan (FuDevice *device, GError **error)
{
	FuSynapticsMstDevice *self = FU_SYNAPTICS_MST_DEVICE (device);
	FuQuirks *quirks;
	guint8 buf_vid[4];
	g_autoptr(FuSynapticsMstConnection) connection = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autofree gchar *version = NULL;
	g_autofree gchar *guid1 = NULL;
	g_autofree gchar *guid2 = NULL;
	g_autofree gchar *guid3 = NULL;
	g_autofree gchar *group = NULL;
	g_autofree gchar *name = NULL;
	const gchar *guid_template;
	const gchar *name_parent;
	const gchar *name_family;
	guint8 buf_ver[16];

	/* read vendor ID */
	connection = fu_synaptics_mst_connection_new (fu_udev_device_get_fd (FU_UDEV_DEVICE (self)), 0, 0);
	if (!fu_synaptics_mst_connection_read (connection, REG_RC_CAP, buf_vid, 1, error)) {
		g_prefix_error (error, "failed to read device: ");
		return FALSE;
	}
	if (buf_vid[0] & 0x04) {
		if (!fu_synaptics_mst_connection_read (connection, REG_VENDOR_ID,
						       buf_vid, 3, error)) {
			g_prefix_error (error, "failed to read vendor ID: ");
			return FALSE;
		}
		/* not a correct device */
		if (buf_vid[0] != 0x90 || buf_vid[1] != 0xCC || buf_vid[2] != 0x24) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "no device");
			return FALSE;
		}
	}

	/* direct */
	self->mode = FU_SYNAPTICS_MST_MODE_DIRECT;
	self->layer = 0;
	self->rad = 0;

	/* enable remote control and disable on exit */
	locker = fu_device_locker_new_full (self,
					    (FuDeviceLockerFunc) fu_synaptics_mst_device_enable_rc,
					    (FuDeviceLockerFunc) fu_synaptics_mst_device_disable_rc,
					    error);
	if (locker == NULL)
		return FALSE;

	/* read firmware version */
	if (!fu_synaptics_mst_connection_read (connection, REG_FIRMWARE_VERSION,
					      buf_ver, 3, error))
		return FALSE;

	version = g_strdup_printf ("%1d.%02d.%02d", buf_ver[0], buf_ver[1], buf_ver[2]);
	fu_device_set_version (FU_DEVICE (self), version, FWUPD_VERSION_FORMAT_TRIPLET);

	/* read board ID */
	if (!fu_synaptics_mst_device_read_board_id (self, connection, buf_ver, error))
		return FALSE;
	self->board_id = fu_common_read_uint16 (buf_ver, G_BIG_ENDIAN);

	/* read board chip_id */
	if (!fu_synaptics_mst_connection_read (connection, REG_CHIP_ID,
					       buf_ver, 2, error)) {
		g_prefix_error (error, "failed to read chip id: ");
		return FALSE;
	}
	self->chip_id = (buf_ver[0] << 8) | (buf_ver[1]);
	if (self->chip_id == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid chip ID");
		return FALSE;
	}
	self->family = fu_synaptics_mst_family_from_chip_id (self->chip_id);

	/* check the active bank for debugging */
	if (self->family == FU_SYNAPTICS_MST_FAMILY_PANAMERA) {
		if (!fu_synaptics_mst_device_get_active_bank_panamera (self, error))
			return FALSE;
	}

	/* recursively look for cascade devices */
	g_clear_object (&locker);
	if (!fu_synaptics_mst_device_scan_cascade (self, 0, error))
		return FALSE;

	/* set up the device name via quirks */
	group = g_strdup_printf ("SynapticsMSTBoardID=%u", self->board_id);
	quirks = fu_device_get_quirks (FU_DEVICE (self));
	name_parent = fu_quirks_lookup_by_id (quirks, group, FU_QUIRKS_NAME);
	if (name_parent != NULL) {
		name = g_strdup_printf ("VMM%04x inside %s",
					self->chip_id, name_parent);
	} else {
		name = g_strdup_printf ("VMM%04x", self->chip_id);
	}
	fu_device_set_name (FU_DEVICE (self), name);

	/* this is a host system, use system ID */
	guid_template = fu_quirks_lookup_by_id (quirks, group, "DeviceKind");
	name_family = fu_synaptics_mst_family_to_string (self->family);
	if (g_strcmp0 (guid_template, "system") == 0) {
		g_autofree gchar *guid = NULL;
		guid = g_strdup_printf ("MST-%s-%s-%u",
					name_family,
					self->system_type,
					self->board_id);
		fu_device_add_instance_id (FU_DEVICE (self), guid);

	/* docks or something else */
	} else if (guid_template != NULL) {
		g_auto(GStrv) templates = NULL;
		templates = g_strsplit (guid_template, ",", -1);
		for (guint i = 0; templates[i] != NULL; i++) {
			g_autofree gchar *dock_id1 = NULL;
			g_autofree gchar *dock_id2 = NULL;
			dock_id1 = g_strdup_printf ("MST-%s-%u",
						    templates[i],
						    self->board_id);
			fu_device_add_instance_id (FU_DEVICE (self), dock_id1);
			dock_id2 = g_strdup_printf ("MST-%s-vmm%04x-%u",
						    templates[i],
						    self->chip_id,
						    self->board_id);
			fu_device_add_instance_id (FU_DEVICE (self), dock_id2);
		}
	}

	/* detect chip family */
	switch (self->family) {
	case FU_SYNAPTICS_MST_FAMILY_TESLA:
		fu_device_set_firmware_size_max (device, 0x10000);
		fu_device_add_instance_id (device, "MST-tesla");
		break;
	case FU_SYNAPTICS_MST_FAMILY_LEAF:
		fu_device_set_firmware_size_max (device, 0x10000);
		fu_device_add_instance_id (device, "MST-leaf");
		break;
	case FU_SYNAPTICS_MST_FAMILY_PANAMERA:
		fu_device_set_firmware_size_max (device, 0x80000);
		fu_device_add_instance_id (device, "MST-panamera");
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_DUAL_IMAGE);
		break;
	default:
		break;
	}

	/* add non-standard GUIDs */
	guid1 = g_strdup_printf ("MST-%s-vmm%04x-%u", name_family, self->chip_id, self->board_id);
	fu_device_add_instance_id (FU_DEVICE (self), guid1);
	guid2 = g_strdup_printf ("MST-%s-%u", name_family, self->board_id);
	fu_device_add_instance_id (FU_DEVICE (self), guid2);
	guid3 = g_strdup_printf ("MST-%s", name_family);
	fu_device_add_instance_id (FU_DEVICE (self), guid3);

	/* success */
	return TRUE;
}

static void
fu_synaptics_mst_device_class_init (FuSynapticsMstDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUdevDeviceClass *klass_udev_device = FU_UDEV_DEVICE_CLASS (klass);
	object_class->finalize = fu_synaptics_mst_device_finalize;
	klass_device->to_string = fu_synaptics_mst_device_to_string;
	klass_device->rescan = fu_synaptics_mst_device_rescan;
	klass_device->write_firmware = fu_synaptics_mst_device_write_firmware;
	klass_device->prepare_firmware = fu_synaptics_mst_device_prepare_firmware;
	klass_udev_device->probe = fu_synaptics_mst_device_probe;
}
