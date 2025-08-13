/*
 * Copyright 2025 Joe Hong <joe_hung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ilitek-its-block.h"
#include "fu-ilitek-its-common.h"
#include "fu-ilitek-its-firmware.h"
#include "fu-ilitek-its-struct.h"

#define FU_ILITEK_ITS_FIRMWARE_MAX_BLOB_SIZE (256 * 1024)

struct _FuIlitekItsFirmware {
	FuIhexFirmware parent_instance;
	guint32 mm_addr;
	gchar *fw_ic_name;
};

G_DEFINE_TYPE(FuIlitekItsFirmware, fu_ilitek_its_firmware, FU_TYPE_IHEX_FIRMWARE)

static void
fu_ilitek_its_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuIlitekItsFirmware *self = FU_ILITEK_ITS_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kv(bn, "fw_ic_name", self->fw_ic_name);
	fu_xmlb_builder_insert_kx(bn, "mm_addr", self->mm_addr);
}

static gboolean
fu_ilitek_its_firmware_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     FuFirmwareParseFlags flags,
			     GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_CLASS(fu_ilitek_its_firmware_parent_class);
	FuIlitekItsFirmware *self = FU_ILITEK_ITS_FIRMWARE(firmware);
	GPtrArray *records = fu_ihex_firmware_get_records(FU_IHEX_FIRMWARE(firmware));
	FuIhexFirmwareRecord *rcd = NULL;
	guint32 mm_ver;
	guint32 start_addr;
	guint8 block_num;
	const guint8 *ic_name = NULL;
	g_autoptr(FuStructIlitekItsMmInfo) st_mm = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) hex_blob = NULL;
	const gchar *ap_end_tag =
	    "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFFILITek AP CRC   ";
	const gchar *block_end_tag =
	    "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFFILITek END TAG  ";

	/* first line is ILITEK-specific record type as memory mapping addr. */
	rcd = g_ptr_array_index(records, 0);
	if (!fu_memread_uint24_safe(rcd->data->data,
				    rcd->data->len,
				    0,
				    &self->mm_addr,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;
	g_ptr_array_remove_index(records, 0);

	if (!klass->parse(firmware, stream, flags, error))
		return FALSE;

	hex_blob = fu_firmware_get_bytes(firmware, error);
	if (hex_blob == NULL)
		return FALSE;

	start_addr = fu_firmware_get_addr(firmware);
	/* fill 0xFF data before start address and after end address */
	fu_byte_array_set_size(buf, start_addr - buf->len, 0xFF);
	fu_byte_array_append_bytes(buf, hex_blob);
	fu_byte_array_set_size(buf, FU_ILITEK_ITS_FIRMWARE_MAX_BLOB_SIZE, 0xFF);
	blob = g_byte_array_free_to_bytes(g_steal_pointer(&buf)); /* nocheck:blocked */
	st_mm = fu_struct_ilitek_its_mm_info_parse_bytes(blob, self->mm_addr, error);
	if (st_mm == NULL)
		return FALSE;

	mm_ver = fu_struct_ilitek_its_mm_info_get_mapping_ver(st_mm);
	ic_name = fu_struct_ilitek_its_mm_info_get_ic_name(st_mm, NULL);
	g_debug("mm ver: 0x%06x, protocol ver: 0x%06x",
		mm_ver,
		fu_struct_ilitek_its_mm_info_get_protocol_ver(st_mm));

	g_free(self->fw_ic_name);
	switch ((mm_ver >> 16) & 0xFF) {
	case 0x02:
		self->fw_ic_name = g_strdup_printf("%s", (gchar *)ic_name);
		break;
	case 0x01:
	default:
		self->fw_ic_name = g_strdup_printf("%02x%02x", ic_name[1], ic_name[0]);
		break;
	}
	block_num = fu_struct_ilitek_its_mm_info_get_block_num(st_mm);
	if (block_num == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "block_num was zero");
		return FALSE;
	}
	for (guint8 i = 0; i < block_num; i++) {
		const gchar *tag = (i == 0) ? ap_end_tag : block_end_tag;
		guint32 start;
		guint32 end;
		gsize offset;
		g_autoptr(FuFirmware) block_img = fu_ilitek_its_block_new();
		g_autoptr(FuStructIlitekItsBlockInfo) st_block = NULL;
		g_autoptr(GBytes) block_fw_fixed = NULL;
		g_autoptr(GBytes) block_fw = NULL;

		st_block = fu_struct_ilitek_its_mm_info_get_blocks(st_mm, i);
		start = fu_struct_ilitek_its_block_info_get_addr(st_block);
		end = fu_struct_ilitek_its_mm_info_get_end_addr(st_mm);

		if (i != block_num - 1) {
			g_autoptr(FuStructIlitekItsBlockInfo) st_block_next =
			    fu_struct_ilitek_its_mm_info_get_blocks(st_mm, i + 1);
			end = fu_struct_ilitek_its_block_info_get_addr(st_block_next);
		}

		/* sanity check */
		if (end < start) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "start 0x%x > end 0x%x",
				    start,
				    end);
			return FALSE;
		}

		block_fw = fu_bytes_new_offset(blob, start, end - start + 1, error);
		if (block_fw == NULL)
			return FALSE;
		if (fu_memmem_safe(g_bytes_get_data(block_fw, NULL),
				   g_bytes_get_size(block_fw),
				   (guint8 *)tag,
				   strlen(tag),
				   &offset,
				   NULL)) {
			offset = offset + strlen(tag) + 2;
			end = start + offset - 1;
			block_fw_fixed = fu_bytes_new_offset(blob, start, offset, error);
		} else {
			block_fw_fixed = g_bytes_ref(block_fw);
		}
		if (!fu_firmware_parse_bytes(block_img,
					     block_fw_fixed,
					     0x0,
					     flags | FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
					     error)) {
			g_prefix_error(error, "failed to parse block %u: ", i);
			return FALSE;
		}
		g_debug("block %u: start 0x%08x, end 0x%08x, crc: 0x%x",
			i,
			start,
			end,
			fu_ilitek_its_block_get_crc(FU_ILITEK_ITS_BLOCK(block_img)));

		fu_firmware_set_offset(block_img, offset);
		fu_firmware_set_idx(block_img, i);
		fu_firmware_set_parent(block_img, firmware);
		fu_firmware_set_addr(block_img, start);
		fu_firmware_add_image(firmware, block_img);
	}

	/* success */
	return TRUE;
}

static gchar *
fu_ilitek_its_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_ilitek_its_convert_version(version_raw);
}

const gchar *
fu_ilitek_its_firmware_get_ic_name(FuIlitekItsFirmware *self)
{
	return self->fw_ic_name;
}

static void
fu_ilitek_its_firmware_init(FuIlitekItsFirmware *self)
{
	fu_ihex_firmware_set_padding_value(FU_IHEX_FIRMWARE(self), 0xFF);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 100);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_QUAD);
}

static void
fu_ilitek_its_firmware_finalize(GObject *object)
{
	FuIlitekItsFirmware *self = FU_ILITEK_ITS_FIRMWARE(object);
	g_free(self->fw_ic_name);
	G_OBJECT_CLASS(fu_ilitek_its_firmware_parent_class)->finalize(object);
}

static void
fu_ilitek_its_firmware_class_init(FuIlitekItsFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_ilitek_its_firmware_finalize;
	firmware_class->convert_version = fu_ilitek_its_firmware_convert_version;
	firmware_class->parse = fu_ilitek_its_firmware_parse;
	firmware_class->export = fu_ilitek_its_firmware_export;
}

FuFirmware *
fu_ilitek_its_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ILITEK_ITS_FIRMWARE, NULL));
}
