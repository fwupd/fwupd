/*
 * Copyright 2025 Joe Hong <JoeHung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ilitek-its-common.h"
#include "fu-ilitek-its-firmware.h"
#include "fu-ilitek-its-struct.h"

struct _FuIlitekItsFirmware {
	FuIhexFirmware parent_instance;

	guint32 mm_addr;
	gchar *fw_ic_name;
	guint8 block_num;

	guint16 block_crc[10];
};

G_DEFINE_TYPE(FuIlitekItsFirmware, fu_ilitek_its_firmware, FU_TYPE_IHEX_FIRMWARE)

static void
fu_ilitek_its_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuIlitekItsFirmware *self = FU_ILITEK_ITS_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kv(bn, "fw_ic_name", self->fw_ic_name);
	fu_xmlb_builder_insert_kx(bn, "mm_addr", self->mm_addr);
	fu_xmlb_builder_insert_kx(bn, "block_num", self->block_num);
}

static gboolean
fu_ilitek_its_firmware_validate(FuFirmware *firmware,
				GInputStream *stream,
				gsize offset,
				GError **error)
{
	g_debug("[%s] pass", __func__);
	// FIXME -- delete this?
	return TRUE;
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

	fu_byte_array_append_bytes(buf, fu_bytes_pad(g_bytes_new(NULL, 0), start_addr, 0xFF));
	fu_byte_array_append_bytes(buf, hex_blob);

	/* FIXME: define magic number */
	blob = fu_bytes_pad(g_bytes_new(buf->data, buf->len), 256 * 1024, 0xFF);

	st_mm = fu_struct_ilitek_its_mm_info_parse_bytes(blob, self->mm_addr, error);
	if (st_mm == NULL)
		return FALSE;

	mm_ver = fu_struct_ilitek_its_mm_info_get_mapping_ver(st_mm);
	ic_name = fu_struct_ilitek_its_mm_info_get_ic_name(st_mm, NULL);
	g_debug("mm ver: 0x%06x, protocol ver: 0x%06x",
		mm_ver,
		fu_struct_ilitek_its_mm_info_get_protocol_ver(st_mm));

	// TODO: make sure u8 ic_name and fw_ic_name handled in the right way
	g_free(self->fw_ic_name);
	switch ((mm_ver >> 16) & 0xff) {
	case 0x02:
		self->fw_ic_name = g_strdup_printf("%s", (gchar *)ic_name);
		break;
	case 0x01:
	default:
		self->fw_ic_name = g_strdup_printf("%02x%02x", ic_name[1], ic_name[0]);
		break;
	}
	self->block_num = fu_struct_ilitek_its_mm_info_get_block_num(st_mm);
	fu_firmware_set_images_max(firmware, self->block_num);

	for (guint i = 0; i < self->block_num; i++) {
		g_autoptr(FuFirmware) block_img = fu_firmware_new();
		g_autoptr(GBytes) block_bytes = NULL;
		g_autoptr(FuStructIlitekItsBlockInfo) block = NULL;
		const gchar *tag = (i == 0) ? ap_end_tag : block_end_tag;
		guint32 start, end;
		gsize offset;

		block = fu_struct_ilitek_its_mm_info_get_blocks(st_mm, i);
		start = fu_struct_ilitek_its_block_info_get_addr(block);
		end = fu_struct_ilitek_its_mm_info_get_end_addr(st_mm);

		if ((i + 1) != self->block_num) {
			block = fu_struct_ilitek_its_mm_info_get_blocks(st_mm, i + 1);
			end = fu_struct_ilitek_its_block_info_get_addr(block);
		}

		if (end < start) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "start 0x%x > end 0x%x",
				    start,
				    end);
			return FALSE;
		}

		block_bytes = fu_bytes_new_offset(blob, start, end - start + 1, error);
		if (block_bytes == NULL)
			return FALSE;

		if (fu_memmem_safe(g_bytes_get_data(block_bytes, NULL),
				   g_bytes_get_size(block_bytes),
				   (guint8 *)tag,
				   strlen(tag),
				   &offset,
				   NULL)) {
			offset = offset + strlen(tag) + 2;
			end = start + offset - 1;
			block_bytes = fu_bytes_new_offset(blob, start, offset, error);
		}

		// FIXME: this should use fu_crc16_bytes() instead
		self->block_crc[i] =
		    fu_ilitek_its_get_crc(block_bytes, g_bytes_get_size(block_bytes) - 2);

		g_debug("block %u: start 0x%08x, end 0x%08x, crc: 0x%x",
			i,
			start,
			end,
			self->block_crc[i]);

		fu_firmware_set_idx(block_img, i);
		fu_firmware_set_parent(block_img, firmware);
		fu_firmware_set_addr(block_img, start);
		fu_firmware_set_bytes(block_img, block_bytes);
		fu_firmware_add_image(firmware, block_img);
	}

	return TRUE;
}

static gchar *
fu_ilitek_its_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	/* convert 8 byte version in to human readable format. e.g. convert 0x0700000101020304 into
	 * 0700.0001.0102.0304*/
	return g_strdup_printf("%02x%02x.%02x%02x.%02x%02x.%02x%02x",
			       (guint)((version_raw >> 56) & 0xFF),
			       (guint)((version_raw >> 48) & 0xFF),
			       (guint)((version_raw >> 40) & 0xFF),
			       (guint)((version_raw >> 32) & 0xFF),
			       (guint)((version_raw >> 24) & 0xFF),
			       (guint)((version_raw >> 16) & 0xFF),
			       (guint)((version_raw >> 8) & 0xFF),
			       (guint)((version_raw >> 0) & 0xFF));
}

const gchar *
fu_ilitek_its_firmware_get_ic_name(FuIlitekItsFirmware *self)
{
	return self->fw_ic_name;
}

guint8
fu_ilitek_its_firmware_get_block_num(FuIlitekItsFirmware *self)
{
	return self->block_num;
}

guint16
fu_ilitek_its_firmware_get_block_crc(FuIlitekItsFirmware *self, guint8 block_num)
{
	if (block_num >= self->block_num) {
		g_warning("block num %u is out of range, max is %u", block_num, self->block_num);
		return 0;
	}

	return self->block_crc[block_num];
}

static void
fu_ilitek_its_firmware_init(FuIlitekItsFirmware *self)
{
	fu_ihex_firmware_set_padding_value(FU_IHEX_FIRMWARE(self), 0xFF);
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
	firmware_class->validate = fu_ilitek_its_firmware_validate;
	firmware_class->parse = fu_ilitek_its_firmware_parse;
	firmware_class->export = fu_ilitek_its_firmware_export;
}

FuFirmware *
fu_ilitek_its_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ILITEK_ITS_FIRMWARE, NULL));
}
