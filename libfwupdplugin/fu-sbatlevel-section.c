/*
 * Copyright 2023 Canonical Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-csv-firmware.h"
#include "fu-input-stream.h"
#include "fu-mem.h"
#include "fu-partial-input-stream.h"
#include "fu-sbatlevel-section-struct.h"
#include "fu-sbatlevel-section.h"

G_DEFINE_TYPE(FuSbatlevelSection, fu_sbatlevel_section, FU_TYPE_FIRMWARE);

static gboolean
fu_sbatlevel_section_add_entry(FuFirmware *firmware,
			       GInputStream *stream,
			       gsize offset,
			       const gchar *entry_name,
			       guint64 entry_idx,
			       FuFirmwareParseFlags flags,
			       GError **error)
{
	gsize streamsz = 0;
	g_autoptr(FuFirmware) entry_fw = NULL;
	g_autoptr(GInputStream) partial_stream = NULL;

	/* stop at the null terminator */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	for (guint i = offset; i < streamsz; i++) {
		guint8 value = 0;
		if (!fu_input_stream_read_u8(stream, i, &value, error))
			return FALSE;
		if (value == 0x0) {
			streamsz = i - 1;
			break;
		}
	}

	entry_fw = fu_csv_firmware_new();
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(entry_fw), "$id");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(entry_fw), "component_generation");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(entry_fw), "date_stamp");
	fu_csv_firmware_set_write_column_ids(FU_CSV_FIRMWARE(entry_fw), FALSE);

	fu_firmware_set_idx(entry_fw, entry_idx);
	fu_firmware_set_id(entry_fw, entry_name);
	fu_firmware_set_offset(entry_fw, offset);
	partial_stream = fu_partial_input_stream_new(stream, offset, streamsz - offset, error);
	if (partial_stream == NULL) {
		g_prefix_error(error, "failed to cut CSV section: ");
		return FALSE;
	}
	if (!fu_firmware_parse_stream(entry_fw, partial_stream, 0, flags, error)) {
		g_prefix_error(error, "failed to parse %s: ", entry_name);
		return FALSE;
	}
	if (!fu_firmware_add_image_full(firmware, entry_fw, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_sbatlevel_section_parse(FuFirmware *firmware,
			   GInputStream *stream,
			   FuFirmwareParseFlags flags,
			   GError **error)
{
	g_autoptr(GByteArray) st = NULL;

	st = fu_struct_sbat_level_section_header_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	if (!fu_sbatlevel_section_add_entry(
		firmware,
		stream,
		sizeof(guint32) + fu_struct_sbat_level_section_header_get_previous(st),
		"previous",
		0,
		flags,
		error))
		return FALSE;
	if (!fu_sbatlevel_section_add_entry(firmware,
					    stream,
					    sizeof(guint32) +
						fu_struct_sbat_level_section_header_get_latest(st),
					    "latest",
					    1,
					    flags,
					    error))
		return FALSE;
	return TRUE;
}

static GByteArray *
fu_sbatlevel_section_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(FuFirmware) img_ltst = NULL;
	g_autoptr(FuFirmware) img_prev = NULL;
	g_autoptr(GByteArray) buf = fu_struct_sbat_level_section_header_new();
	g_autoptr(GBytes) blob_ltst = NULL;
	g_autoptr(GBytes) blob_prev = NULL;

	/* previous */
	fu_struct_sbat_level_section_header_set_previous(buf, sizeof(guint32) * 2);
	img_prev = fu_firmware_get_image_by_id(firmware, "previous", error);
	if (img_prev == NULL)
		return NULL;
	blob_prev = fu_firmware_write(img_prev, error);
	if (blob_prev == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob_prev);
	fu_byte_array_append_uint8(buf, 0x0);

	/* latest */
	fu_struct_sbat_level_section_header_set_latest(buf,
						       (sizeof(guint32) * 2) +
							   g_bytes_get_size(blob_prev) + 1);
	img_ltst = fu_firmware_get_image_by_id(firmware, "latest", error);
	if (img_ltst == NULL)
		return NULL;
	blob_ltst = fu_firmware_write(img_ltst, error);
	if (blob_ltst == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob_ltst);
	fu_byte_array_append_uint8(buf, 0x0);

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_sbatlevel_section_init(FuSbatlevelSection *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), 2);
}

static void
fu_sbatlevel_section_class_init(FuSbatlevelSectionClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);

	firmware_class->parse = fu_sbatlevel_section_parse;
	firmware_class->write = fu_sbatlevel_section_write;
}

FuFirmware *
fu_sbatlevel_section_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_SBATLEVEL_SECTION, NULL));
}
