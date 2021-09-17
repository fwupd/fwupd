/*
 * Copyright (C) 2021 Michael Cheng <michael.cheng@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "fu-elanfp-file-control.h"

#define SIZE_IDENTIFY_PACKET 16

#define S2F_TAG_FIRMWAREVERSION 0x00
#define S2F_TAG_CFU_OFFER_A	0x72
#define S2F_TAG_CFU_OFFER_B	0x73
#define S2F_TAG_CFU_PAYLOAD_A	0x74
#define S2F_TAG_CFU_PAYLOAD_B	0x75
#define S2F_TAG_END_OF_INDEX	0xFF

gboolean
fU_elanfp_file_ctrl_binary_verify(FuFirmware *firmware, GError **error)
{
	S2F_HEADER *ps2f_header = NULL;
	S2F_INDEX *ps2f_index = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw_index = NULL;
	g_autoptr(GBytes) fw_offer_a = NULL;
	g_autoptr(GBytes) fw_offer_b = NULL;
	g_autoptr(GBytes) fw_payload_a = NULL;
	g_autoptr(GBytes) fw_payload_b = NULL;
	g_autoptr(FuFirmware) img_offer_a = fu_firmware_new();
	g_autoptr(FuFirmware) img_offer_b = fu_firmware_new();
	g_autoptr(FuFirmware) img_payload_a = fu_firmware_new();
	g_autoptr(FuFirmware) img_payload_b = fu_firmware_new();
	gsize binary_size;
	gsize index_size;

	g_return_val_if_fail(firmware != NULL, FALSE);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL) {
		g_prefix_error(error, "binary verify - fail to get fw buffer: ");
		return FALSE;
	}

	/* check the file size */
	ps2f_header = (S2F_HEADER *)g_bytes_get_data(fw, &binary_size);

	if (binary_size == 0) {
		g_prefix_error(error, "binary verify - file size is zero: ");
		return FALSE;
	}

	if (GUINT32_FROM_LE(ps2f_header->Tag) != 0x46325354) {
		g_prefix_error(error, "binary verify - file tag is not correct: ");
		return FALSE;
	}

	g_debug("s2f format version: 0x%08X", GUINT32_FROM_LE(ps2f_header->FormatVersion));

	/* find index */
	fw_index = fu_common_bytes_new_offset(fw, sizeof(S2F_HEADER), sizeof(S2F_INDEX), error);

	ps2f_index = (S2F_INDEX *)g_bytes_get_data(fw_index, &index_size);

	while (1) {
		switch (ps2f_index->Type) {
		case S2F_TAG_CFU_OFFER_A:

			fw_offer_a = fu_common_bytes_new_offset(fw,
								ps2f_index->StartAddress,
								ps2f_index->Length,
								error);

			fu_firmware_set_id(img_offer_a, FW_SET_ID_OFFER_A);
			fu_firmware_set_bytes(img_offer_a, fw_offer_a);
			fu_firmware_add_image(firmware, img_offer_a);

			break;
		case S2F_TAG_CFU_OFFER_B:

			fw_offer_b = fu_common_bytes_new_offset(fw,
								ps2f_index->StartAddress,
								ps2f_index->Length,
								error);

			fu_firmware_set_id(img_offer_b, FW_SET_ID_OFFER_B);
			fu_firmware_set_bytes(img_offer_b, fw_offer_b);
			fu_firmware_add_image(firmware, img_offer_b);

			break;
		case S2F_TAG_CFU_PAYLOAD_A:

			fw_payload_a = fu_common_bytes_new_offset(fw,
								  ps2f_index->StartAddress,
								  ps2f_index->Length,
								  error);

			fu_firmware_set_id(img_payload_a, FW_SET_ID_PAYLOAD_A);
			fu_firmware_set_bytes(img_payload_a, fw_payload_a);
			fu_firmware_add_image(firmware, img_payload_a);

			break;
		case S2F_TAG_CFU_PAYLOAD_B:

			fw_payload_b = fu_common_bytes_new_offset(fw,
								  ps2f_index->StartAddress,
								  ps2f_index->Length,
								  error);

			fu_firmware_set_id(img_payload_b, FW_SET_ID_PAYLOAD_B);
			fu_firmware_set_bytes(img_payload_b, fw_payload_b);
			fu_firmware_add_image(firmware, img_payload_b);

			break;
		case S2F_TAG_END_OF_INDEX:

			g_debug("binary verify - end of index");

			return TRUE;

		default:;
		}

		ps2f_index++;
	}

	g_debug("binary verify - success");

	return TRUE;
}
