/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-common.h"
#include "fu-wac-common.h"

guint32
fu_wac_calculate_checksum32le (const guint8 *data, gsize len)
{
	guint32 csum = 0x0;
	g_return_val_if_fail (len % 4 == 0, 0xff);
	for (guint i = 0; i < len; i += 4) {
		guint32 tmp;
		memcpy (&tmp, &data[i], sizeof(guint32));
		csum += GUINT32_FROM_LE (tmp);
	}
	return GUINT32_TO_LE (csum);
}

guint32
fu_wac_calculate_checksum32le_bytes (GBytes *blob)
{
	gsize len = 0;
	const guint8 *data = g_bytes_get_data (blob, &len);
	return fu_wac_calculate_checksum32le (data, len);
}

const gchar *
fu_wac_report_id_to_string (guint8 report_id)
{
	if (report_id == FU_WAC_REPORT_ID_FW_DESCRIPTOR)
		return "FwDescriptor";
	if (report_id == FU_WAC_REPORT_ID_SWITCH_TO_FLASH_LOADER)
		return "SwitchToFlashLoader";
	if (report_id == FU_WAC_REPORT_ID_QUIT_AND_RESET)
		return "QuitAndReset";
	if (report_id == FU_WAC_REPORT_ID_READ_BLOCK_DATA)
		return "ReadBlockData";
	if (report_id == FU_WAC_REPORT_ID_WRITE_BLOCK)
		return "WriteBlock";
	if (report_id == FU_WAC_REPORT_ID_ERASE_BLOCK)
		return "EraseBlock";
	if (report_id == FU_WAC_REPORT_ID_SET_READ_ADDRESS)
		return "SetReadAddress";
	if (report_id == FU_WAC_REPORT_ID_GET_STATUS)
		return "GetStatus";
	if (report_id == FU_WAC_REPORT_ID_UPDATE_RESET)
		return "UpdateReset";
	if (report_id == FU_WAC_REPORT_ID_WRITE_WORD)
		return "WriteWord";
	if (report_id == FU_WAC_REPORT_ID_GET_PARAMETERS)
		return "GetParameters";
	if (report_id == FU_WAC_REPORT_ID_GET_FLASH_DESCRIPTOR)
		return "GetFlashDescriptor";
	if (report_id == FU_WAC_REPORT_ID_GET_CHECKSUMS)
		return "GetChecksums";
	if (report_id == FU_WAC_REPORT_ID_SET_CHECKSUM_FOR_BLOCK)
		return "SetChecksumForBlock";
	if (report_id == FU_WAC_REPORT_ID_CALCULATE_CHECKSUM_FOR_BLOCK)
		return "CalculateChecksumForBlock";
	if (report_id == FU_WAC_REPORT_ID_WRITE_CHECKSUM_TABLE)
		return "WriteChecksumTable";
	if (report_id == FU_WAC_REPORT_ID_GET_CURRENT_FIRMWARE_IDX)
		return "GetCurrentFirmwareIdx";
	if (report_id == FU_WAC_REPORT_ID_MODULE)
		return "Module";
	return NULL;
}

void
fu_wac_buffer_dump (const gchar *title, guint8 cmd, const guint8 *buf, gsize sz)
{
	g_autofree gchar *tmp = NULL;
	if (g_getenv ("FWUPD_WACOM_USB_VERBOSE") == NULL)
		return;
	tmp = g_strdup_printf ("%s %s (%" G_GSIZE_FORMAT ")",
			       title, fu_wac_report_id_to_string (cmd), sz);
	fu_common_dump_raw (G_LOG_DOMAIN, tmp, buf, sz);
}
