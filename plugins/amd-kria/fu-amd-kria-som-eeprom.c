/*
 * Copyright 2024 Advanced Micro Devices Inc.
 * All rights reserved.
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-amd-kria-som-eeprom-struct.h"
#include "fu-amd-kria-som-eeprom.h"

struct _FuAmdKriaSomEeprom {
	FuFirmwareClass parent_instance;
	gchar *manufacturer;
	gchar *product_name;
	gchar *serial_number;
};

G_DEFINE_TYPE(FuAmdKriaSomEeprom, fu_amd_kria_som_eeprom, FU_TYPE_FIRMWARE)

/* IPMI spec encodes 0:5 as length and 6:7 as "type" code */
#define LENGTH(data)	data & 0x3f
#define TYPE_CODE(data) data >> 6

static gboolean
fu_amd_kria_som_eeprom_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuAmdKriaSomEeprom *self = FU_AMD_KRIA_SOM_EEPROM(firmware);
	guint8 str_len = 0;
	guint8 str_offset = FU_STRUCT_BOARD_INFO_OFFSET_MANUFACTURER_LEN;
	guint8 board_offset;
	const guint8 *buf;
	gsize bufsz = 0;
	g_autoptr(FuStructIpmiCommon) common = NULL;
	g_autoptr(FuStructBoardInfo) board = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* parse IPMI common header */
	common = fu_struct_ipmi_common_parse_stream(stream, 0x0, error);
	if (common == NULL)
		return FALSE;
	board_offset = fu_struct_ipmi_common_get_board_offset(common) * 8;

	/* parse board info area */
	board = fu_struct_board_info_parse_stream(stream, board_offset, error);
	if (board == NULL)
		return FALSE;

	fw = fu_input_stream_read_bytes(stream,
					board_offset,
					fu_struct_board_info_get_length(board) * 8,
					error);
	if (fw == NULL)
		return FALSE;
	buf = fu_bytes_get_data_safe(fw, &bufsz, error);
	if (buf == NULL)
		return FALSE;

	/* manufacturer string in board area */
	str_offset = str_offset + str_len;
	str_len = LENGTH(buf[str_offset]);
	str_offset++;
	self->manufacturer = fu_strsafe((gchar *)buf + str_offset, str_len);

	str_offset = str_offset + str_len;
	str_len = LENGTH(buf[str_offset]);
	str_offset++;
	self->product_name = fu_strsafe((gchar *)buf + str_offset, str_len);

	str_offset = str_offset + str_len;
	str_len = LENGTH(buf[str_offset]);
	str_offset++;
	self->serial_number = fu_strsafe((gchar *)buf + str_offset, str_len);

	return TRUE;
}

const gchar *
fu_amd_kria_som_eeprom_get_manufacturer(FuAmdKriaSomEeprom *self)
{
	return self->manufacturer;
}

const gchar *
fu_amd_kria_som_eeprom_get_product_name(FuAmdKriaSomEeprom *self)
{
	return self->product_name;
}

const gchar *
fu_amd_kria_som_eeprom_get_serial_number(FuAmdKriaSomEeprom *self)
{
	return self->serial_number;
}

static void
fu_amd_kria_som_eeprom_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuAmdKriaSomEeprom *self = FU_AMD_KRIA_SOM_EEPROM(firmware);

	fu_xmlb_builder_insert_kv(bn, "manufacturer", self->manufacturer);
	fu_xmlb_builder_insert_kv(bn, "product_name", self->product_name);
	fu_xmlb_builder_insert_kv(bn, "serial_number", self->serial_number);
}

static void
fu_amd_kria_som_eeprom_init(FuAmdKriaSomEeprom *self)
{
}

static void
fu_amd_kria_som_eeprom_finalize(GObject *object)
{
	FuAmdKriaSomEeprom *self = FU_AMD_KRIA_SOM_EEPROM(object);

	g_free(self->manufacturer);
	g_free(self->product_name);
	g_free(self->serial_number);

	G_OBJECT_CLASS(fu_amd_kria_som_eeprom_parent_class)->finalize(object);
}
static void
fu_amd_kria_som_eeprom_class_init(FuAmdKriaSomEepromClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_amd_kria_som_eeprom_finalize;
	firmware_class->parse = fu_amd_kria_som_eeprom_parse;
	firmware_class->export = fu_amd_kria_som_eeprom_export;
}

FuFirmware *
fu_amd_kria_som_eeprom_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_AMD_KRIA_SOM_EEPROM, NULL));
}
