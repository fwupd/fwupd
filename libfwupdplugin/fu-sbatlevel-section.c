/*
 * Copyright (C) 2023 Canonical Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-bytes.h"
#include "fu-csv-firmware.h"
#include "fu-mem.h"
#include "fu-sbatlevel-section-struct.h"
#include "fu-sbatlevel-section.h"

G_DEFINE_TYPE(FuSbatlevelSection, fu_sbatlevel_section, FU_TYPE_FIRMWARE);

static gboolean
fu_sbatlevel_section_add_entry(FuFirmware *firmware,
			       GBytes *fw,
			       const guint8 *buf,
			       gsize bufsz,
			       gsize entry_offset,
			       const gchar *entry_name,
			       guint64 entry_idx,
			       FwupdInstallFlags flags,
			       GError **error)
{
	g_autoptr(FuFirmware) entry_fw = NULL;
	g_autoptr(GBytes) entry_blob = NULL;
	gsize entry_size;

	/* look for the null terminator */
	for (entry_size = 0; ((entry_offset + entry_size) < bufsz); ++entry_size) {
		if (buf[entry_offset + entry_size] == 0)
			break;
	}

	entry_fw = fu_csv_firmware_new();
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(entry_fw), "$id");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(entry_fw), "component_generation");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(entry_fw), "date_stamp");

	fu_firmware_set_idx(entry_fw, entry_idx);
	fu_firmware_set_id(entry_fw, entry_name);
	fu_firmware_set_offset(entry_fw, entry_offset);
	entry_blob = fu_bytes_new_offset(fw, entry_offset, entry_size, error);
	if (entry_blob == NULL)
		return FALSE;
	if (!fu_firmware_add_image_full(firmware, entry_fw, error))
		return FALSE;
	if (!fu_firmware_parse(entry_fw, entry_blob, flags, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_sbatlevel_section_parse(FuFirmware *firmware,
			   GBytes *fw,
			   gsize offset,
			   FwupdInstallFlags flags,
			   GError **error)
{
	const guint8 *buf = NULL;
	gsize bufsz;
	gsize header_offset = offset + FU_STRUCT_SBAT_LEVEL_SECTION_HEADER_OFFSET_PREVIOUS;
	guint32 previous_addr;
	guint32 latest_addr;
	g_autoptr(GByteArray) st = NULL;

	buf = g_bytes_get_data(fw, &bufsz);

	st = fu_struct_sbat_level_section_header_parse(buf, bufsz, offset, error);
	if (st == NULL)
		return FALSE;

	previous_addr = fu_struct_sbat_level_section_header_get_previous(st);
	latest_addr = fu_struct_sbat_level_section_header_get_latest(st);

	if (!fu_sbatlevel_section_add_entry(firmware,
					    fw,
					    buf,
					    bufsz,
					    header_offset + previous_addr,
					    "previous",
					    0,
					    flags,
					    error))
		return FALSE;

	if (!fu_sbatlevel_section_add_entry(firmware,
					    fw,
					    buf,
					    bufsz,
					    header_offset + latest_addr,
					    "latest",
					    1,
					    flags,
					    error))
		return FALSE;

	return TRUE;
}

static void
fu_sbatlevel_section_init(FuSbatlevelSection *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), 2);
}

static void
fu_sbatlevel_section_class_init(FuSbatlevelSectionClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);

	klass_firmware->parse = fu_sbatlevel_section_parse;
}

FuFirmware *
fu_sbatlevel_section_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_SBATLEVEL_SECTION, NULL));
}
