/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#include "fu-analogix-usbc-common.h"

guint64
hex_str_to_dec (const gchar *str, guint8 len)
{
	g_autofree gchar *src_str = NULL;

	if (len > 8 || str == NULL)
		return 0;
	src_str = g_strndup (str, len);

	return g_ascii_strtoull (src_str, NULL, 16);
}

gboolean
parse_fw_hex_file (const guint8 *fw_src, guint32 src_fw_size,
		   AnxImgHeader *out_header, guint8 *out_binary)
{
	gboolean end_of_file = FALSE;
	guint8 addr_mode = 0;
	guint16 most_high_addr = 0;
	gboolean init_addr = TRUE;
	guint32 fw_start_addr = 0;
	guint32 fw_max_addr = 0;
	guint8 last_len = 0;
	gsize fw_index = 0;
	gboolean new_line = TRUE;
	guint8 line_len = 0;
	guint32 start_addr = 0;
	guint8 rec_type = 0;
	const guint8 *hex_hdr = fw_src;
	guint8 *bin_buf = out_binary;
	guint8 checksum;
	guint32 payload_index = 0;
	guint32 base_index = 0;
	guint8 tmp;
	guint32 version_addr = 0;
	guchar colon = ':';
	AnxImgHeader *img_header = out_header;

	if (fw_src == NULL || src_fw_size < HEX_LINE_HEADER_SIZE ||
	    out_header == NULL || out_binary == NULL)
		return FALSE;

	while (end_of_file == FALSE) {
		if (new_line) {
			if (hex_hdr[fw_index] != colon)
				return FALSE;
			new_line = FALSE;
			fw_index++;
			/* caculate start addr */
			line_len = (guint8) hex_str_to_dec ((const gchar*) &hex_hdr[fw_index], 2);
			fw_index += 2;
			start_addr = (guint32) hex_str_to_dec ((const gchar*) &hex_hdr[fw_index], 4);
			fw_index += 4;
			rec_type = (guint8) hex_str_to_dec ((const gchar*) &hex_hdr[fw_index], 2);
			fw_index +=2;
			switch (rec_type) {
			case 0:
				checksum = line_len + rec_type +
                                        (guint8) (start_addr & 0xff) +
					(guint8) ((start_addr >> 8) & 0xff);
				if (addr_mode == 2) {
					/* extend mode */
					start_addr = (most_high_addr << 4) + start_addr;
					version_addr = (most_high_addr << 4) + OCM_FW_VERSION_ADDR;
				}
				else if (addr_mode == 4) {
					start_addr = (most_high_addr << 16) + start_addr;
					version_addr = (most_high_addr << 16) + OCM_FW_VERSION_ADDR;
				}

				/* g_debug  ("start_addr:0x%x", start_addr); */
				switch (start_addr) {
				case FLASH_OCM_ADDR:
					img_header->fw_start_addr = start_addr;
					init_addr = TRUE;
					base_index = 0;
					break;
				case FLASH_TXFW_ADDR:
					img_header->secure_tx_start_addr = start_addr;
					img_header->fw_end_addr = fw_max_addr;
					if (fw_max_addr > fw_start_addr &&
					    img_header->fw_start_addr != 0) {
						img_header->fw_payload_len = fw_max_addr -
							fw_start_addr + last_len;
					}
					init_addr = TRUE;
					base_index = img_header->fw_payload_len;
					break;
				case FLASH_RXFW_ADDR:
					img_header->secure_rx_start_addr = start_addr;
					if (fw_max_addr > fw_start_addr &&
					    img_header->fw_start_addr > 0 &&
					    img_header->fw_payload_len == 0) {
						img_header->fw_payload_len = fw_max_addr -
							fw_start_addr + last_len;
					}
					if (fw_max_addr > fw_start_addr &&
					    img_header->secure_tx_start_addr > 0) {
						img_header->secure_tx_payload_len = fw_max_addr -
							fw_start_addr + last_len;
					}
					init_addr = TRUE;
					base_index = img_header->secure_tx_payload_len + img_header->fw_payload_len;
					break;
				case FLASH_CUSTOM_ADDR:
					img_header->custom_start_addr = start_addr;
					if (fw_max_addr > fw_start_addr &&
					    img_header->fw_start_addr > 0 &&
					    img_header->fw_payload_len == 0) {
						img_header->fw_payload_len = fw_max_addr -
							fw_start_addr + last_len;
					}
					if (fw_max_addr > fw_start_addr &&
					    img_header->secure_tx_start_addr > 0 &&
					    img_header->secure_tx_payload_len == 0) {
						img_header->secure_tx_payload_len = fw_max_addr -
							fw_start_addr + last_len;
					}
					if (fw_max_addr > fw_start_addr &&
					    img_header->secure_rx_start_addr > 0) {
						img_header->secure_rx_payload_len = fw_max_addr -
							fw_start_addr + last_len;
					}
					init_addr = TRUE;
					base_index = img_header->secure_rx_payload_len +
						img_header->secure_tx_payload_len +
						img_header->fw_payload_len;
					break;
				default:
					break;
				}
				if (init_addr) {
					fw_start_addr = start_addr;
					fw_max_addr = start_addr;
					last_len = line_len;
					init_addr = FALSE;
				}
				if (start_addr > fw_max_addr) {
					fw_max_addr = start_addr;
					last_len = line_len;
				}
				payload_index = start_addr - fw_start_addr + base_index;
				for (guint8 i = 0; i < line_len; i++) {
					tmp = (guint8) hex_str_to_dec ((const gchar*) &hex_hdr[fw_index], 2);
					bin_buf[payload_index] = tmp;
					checksum += tmp;
					fw_index += 2;
					payload_index++;
				}
				if (start_addr == version_addr && fw_start_addr == FLASH_OCM_ADDR) {
					/* get ocm version */
					img_header->fw_ver = (guint8) hex_str_to_dec ((const gchar*) &hex_hdr[fw_index - 16], 2);
					img_header->fw_ver = img_header->fw_ver << 8;
					img_header->fw_ver |= (guint8) hex_str_to_dec ((const gchar*) &hex_hdr[fw_index - 8], 2);
				}
				break;
			case 1:
				end_of_file = TRUE;
				break;
			case 2:
				addr_mode = 2;
				most_high_addr = (guint16) hex_str_to_dec ((const gchar*) &hex_hdr[fw_index], 4);
				checksum = line_len + rec_type +
					(guint8) (start_addr & 0xff) +
					(guint8) ((start_addr >> 8)&0xff) +
					(guint8) (most_high_addr & 0xff) +
					(guint8) ((most_high_addr >> 8) & 0xff);
				fw_index += 4;
				break;
			case 4:
				addr_mode = 4;
				most_high_addr = (guint16) hex_str_to_dec ((const gchar*) &hex_hdr[fw_index], 4);
				checksum = line_len + rec_type +
					(guint8) (start_addr & 0xff) +
					(guint8) ((start_addr >> 8)&0xff) +
					(guint8) (most_high_addr & 0xff) +
					(guint8) ((most_high_addr >> 8) & 0xff);
				fw_index += 4;
				break;
			default:
				return FALSE;
			}
			if (end_of_file)
				break;
			checksum = -checksum;
			if (checksum != (guint8) hex_str_to_dec ((const gchar*) &hex_hdr[fw_index], 2))
				fw_index += 2;
			return FALSE;
		}
		else {
			fw_index += 2;
			if (hex_hdr[fw_index] == 0x0D || hex_hdr[fw_index] == 0x0A)
				fw_index ++;
			else
				new_line = TRUE;
		}
	}
	/* only OCM */
	if (img_header->fw_payload_len == 0 && img_header->fw_start_addr != 0)
		img_header->fw_payload_len = fw_max_addr-fw_start_addr + last_len;
	if (img_header->secure_tx_start_addr != 0 && img_header->secure_tx_payload_len == 0)
		img_header->secure_tx_payload_len = fw_max_addr-fw_start_addr + last_len;
	if (img_header->secure_rx_start_addr != 0 && img_header->secure_rx_payload_len == 0)
		img_header->secure_rx_payload_len = fw_max_addr-fw_start_addr + last_len;
	if (img_header->custom_start_addr != 0 && img_header->custom_payload_len == 0)
		img_header->custom_payload_len = fw_max_addr-fw_start_addr + last_len;
	img_header->total_len = img_header->fw_payload_len +
		img_header->secure_tx_payload_len +
		img_header->secure_rx_payload_len +
		img_header->custom_payload_len;
	g_debug ("total len:0x%x", img_header->total_len);
	g_debug ("OCM start: 0x%x, len:0x%x", img_header->fw_start_addr,
		 img_header->fw_payload_len);
	g_debug ("Secure OCM TX start: 0x%x, len:0x%x",
		 img_header->secure_tx_start_addr,
		 img_header->secure_tx_payload_len);
	g_debug ("Secure OCM RX start: 0x%x, len:0x%x",
		 img_header->secure_rx_start_addr,
		 img_header->secure_rx_payload_len);
	g_debug ("Custom start: 0x%x, len:0x%x",
		 img_header->custom_start_addr,
		 img_header->custom_payload_len);
	return TRUE;
}
