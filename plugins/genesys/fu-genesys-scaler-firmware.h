/*
 * Copyright (C) 2021 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GENESYS_SCALER_FIRMWARE (fu_genesys_scaler_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuGenesysScalerFirmware,
		     fu_genesys_scaler_firmware,
		     FU,
		     GENESYS_SCALER_FIRMWARE,
		     FuFirmware)

#define MTK_RSA_HEADER "MTK_RSA_HEADER"

typedef struct __attribute__((packed)) {
	guint8 default_head[14];
	guint8 reserved_0e_0f[2];
	guint8 model_name[16];
	guint8 reserved_20;
	guint8 size[2];
	guint8 reserved_23_27[5];
	guint8 scaler_group[10];
	guint8 reserved_32_53[34];
	guint8 panel_type[10];
	guint8 scaler_packet_date[8];
	guint8 reserved_66_67[2];
	guint8 scaler_packet_version[4];
	guint8 reserved_6c_7f[20];
	union {
		guint8 r8;
		struct {
			/*
			 * Decrypt Mode:
			 *
			 * 0: Scaler decrypt
			 * 1: ISP Tool decrypt
			 */
			guint8 decrypt_mode : 1;

			/*
			 * Second Image:
			 *
			 * 0: 1st image or dual image; programming address at 0x000000
			 * 1: 2nd image; programming address set by .second_image_program_addr
			 */
			guint8 second_image : 1;

			/*
			 * Dual image turn:
			 *
			 * 0: fix second programing address set by .second_image_program_addr
			 * 1: support “Dual image turn” rule
			 *    - TSUM:  Not supported
			 *    - MST9U: ISP Tool need update to DUT least version image address
			 *    - HAWK:  Not supported
			 */
			guint8 dual_image_turn : 1;

			/*
			 * Special Protect Sector:
			 *
			 * 0: No Special Protect sector
			 * 1: Support Special Protect sector
			 */
			guint8 special_protect_sector : 1;

			/*
			 * HAWK bypass mode
			 *
			 * 0: No support HAWK bypass mode
			 * 1: Support HAWK bypass mode
			 */
			guint8 hawk_bypass_mode : 1;

			/*
			 * Boot Code Size in header
			 *
			 * 0: Follow original search bin address rule
			 * 1: Get Boot code size from header set by .boot_code_size
			 */
			guint8 boot_code_size_in_header : 1;

			/* Reserved */
			guint8 reserved_6_7 : 2;
		} __attribute__((packed)) bits;
	} configuration_setting;

	guint8 reserved_81_85[5];

	/* If configuration_setting.bits.second_image set */
	guint8 second_image_program_addr[4];

	/*
	 * If configuration_setting.bits.decrypt is set
	 *
	 * TSUM/HAWK: ISP Tool need protect flash public address can’t erase and write
	 * MST9U: Not supported
	 */
	guint8 scaler_public_key_addr[4];

	/*
	 * If configuration_setting.bits.special_protect_sector is set
	 *
	 * ISP Tool can't erase "Special Protect Sector" area.
	 *
	 * [19:00]: Protect continuous sector start.
	 * [23:20]: Protect sector continuous number.
	 *
	 * Examples: If need to protect FA000 ~FFFFF, Special Protect sector = 0x6000FA;
	 *           If need to protect FA000 only, Special Protect sector = 0x1000FA;
	 *           If no need to protect, Special Protect sector = 0x000000;
	 */
	union {
		guint32 r32;
		struct {
			guint8 addr_low[2];
			guint8 addr_high : 4;
			guint8 size : 4;
		} __attribute__((packed)) area;
	} protect_sector[2];

	/*
	 * If configuration.bits .second_image and .dual_image_turn are set
	 * and .boot_code_size.
	 */
	guint32 boot_code_size;
} FuGenesysMtkRsaHeader;

typedef union __attribute__((packed)) {
	guint8 raw[0x312];
	struct {
		struct {
			guint8 N[0x206];
			guint8 E[0x00c];
		} __attribute__((packed)) public_key;
		FuGenesysMtkRsaHeader header;
	} data;
} FuGenesysMtkFooter;

void
fu_genesys_scaler_firmware_decrypt(guint8 *buf, gsize bufsz);

FuFirmware *
fu_genesys_scaler_firmware_new(void);
