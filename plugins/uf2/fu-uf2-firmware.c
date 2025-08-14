/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uf2-firmware.h"
#include "fu-uf2-struct.h"

struct _FuUf2Firmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuUf2Firmware, fu_uf2_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_uf2_firmware_parse_extensions(FuUf2Firmware *self,
				 const guint8 *buf,
				 gsize bufsz,
				 gsize offset,
				 GError **error)
{
	while (offset < bufsz) {
		guint8 sz = 0;
		FuUf2FirmwareTag tag = 0;
		g_autoptr(FuStructUf2Extension) st_ext = NULL;

		st_ext = fu_struct_uf2_extension_parse(buf, bufsz, offset, error);
		if (st_ext == NULL)
			return FALSE;
		sz = fu_struct_uf2_extension_get_size(st_ext);
		if (sz == 0)
			break;
		if (sz < 4) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid extension tag 0x%x [%s] size 0x%x",
				    tag,
				    fu_uf2_firmware_tag_to_string(tag),
				    (guint)sz);
			return FALSE;
		}
		tag = fu_struct_uf2_extension_get_tag(st_ext);
		if (tag == 0)
			break;
		if (tag == FU_UF2_FIRMWARE_TAG_VERSION) {
			g_autofree gchar *str = NULL;
			str = fu_memstrsafe(buf,
					    bufsz,
					    offset + st_ext->len,
					    sz - st_ext->len,
					    error);
			if (str == NULL)
				return FALSE;
			fu_firmware_set_version(FU_FIRMWARE(self), str);
		} else if (tag == FU_UF2_FIRMWARE_TAG_DESCRIPTION) {
			g_autofree gchar *str = NULL;
			str = fu_memstrsafe(buf,
					    bufsz,
					    offset + st_ext->len,
					    sz - st_ext->len,
					    error);
			if (str == NULL)
				return FALSE;
			fu_firmware_set_id(FU_FIRMWARE(self), str);
		} else {
			if (g_getenv("FWUPD_FUZZER_RUNNING") == NULL) {
				g_warning("unknown tag 0x%06x [%s]",
					  tag,
					  fu_uf2_firmware_tag_to_string(tag));
			}
		}

		/* next! */
		offset += fu_common_align_up(sz, FU_FIRMWARE_ALIGNMENT_4);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uf2_firmware_parse_chunk(FuUf2Firmware *self, FuChunk *chk, GByteArray *tmp, GError **error)
{
	gsize bufsz = fu_chunk_get_data_sz(chk);
	const guint8 *buf = fu_chunk_get_data(chk);
	guint32 flags = 0;
	guint32 datasz = 0;
	g_autoptr(GByteArray) st = NULL;

	/* parse */
	st = fu_struct_uf2_parse(fu_chunk_get_data(chk),
				 fu_chunk_get_data_sz(chk),
				 0, /* offset */
				 error);
	if (st == NULL)
		return FALSE;
	flags = fu_struct_uf2_get_flags(st);
	if (flags & FU_UF2_FIRMWARE_BLOCK_FLAG_IS_CONTAINER) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "container U2F firmware not supported");
		return FALSE;
	}
	datasz = fu_struct_uf2_get_payload_size(st);
	if (datasz > 476) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "data size impossible got 0x%08x",
			    datasz);
		return FALSE;
	}
	if (fu_struct_uf2_get_block_no(st) != fu_chunk_get_idx(chk)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "block count invalid, expected 0x%04x and got 0x%04x",
			    fu_chunk_get_idx(chk),
			    fu_struct_uf2_get_block_no(st));
		return FALSE;
	}
	if (fu_struct_uf2_get_num_blocks(st) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "block count invalid, expected > 0");
		return FALSE;
	}
	if (flags & FU_UF2_FIRMWARE_BLOCK_FLAG_HAS_FAMILY) {
		if (fu_struct_uf2_get_family_id(st) == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "family_id required but not supplied");
			return FALSE;
		}
	}

	/* assume first chunk is representative of firmware */
	if (fu_chunk_get_idx(chk) == 0) {
		fu_firmware_set_addr(FU_FIRMWARE(self), fu_struct_uf2_get_target_addr(st));
		fu_firmware_set_idx(FU_FIRMWARE(self), fu_struct_uf2_get_family_id(st));
	}

	/* just append raw data */
	g_byte_array_append(tmp, fu_struct_uf2_get_data(st, NULL), datasz);
	if (flags & FU_UF2_FIRMWARE_BLOCK_FLAG_HAS_MD5) {
		if (datasz < 24) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "not enough space for MD5 checksum");
			return FALSE;
		}
	}
	if (flags & FU_UF2_FIRMWARE_BLOCK_FLAG_HAS_EXTENSION_TAG) {
		if (!fu_uf2_firmware_parse_extensions(self,
						      buf,
						      bufsz,
						      datasz + FU_STRUCT_UF2_OFFSET_DATA,
						      error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uf2_firmware_parse(FuFirmware *firmware,
		      GInputStream *stream,
		      FuFirmwareParseFlags flags,
		      GError **error)
{
	FuUf2Firmware *self = FU_UF2_FIRMWARE(firmware);
	g_autoptr(GByteArray) tmp = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* read in fixed sized chunks */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						512,
						error);
	if (chunks == NULL)
		return FALSE;
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_uf2_firmware_parse_chunk(self, chk, tmp, error))
			return FALSE;
	}

	/* success */
	blob = g_bytes_new(tmp->data, tmp->len);
	fu_firmware_set_bytes(firmware, blob);
	return TRUE;
}

static FuStructUf2Extension *
fu_uf2_firmware_build_utf8_extension(FuUf2FirmwareTag tag, const gchar *str)
{
	g_autoptr(FuStructUf2Extension) st = fu_struct_uf2_extension_new();
	fu_struct_uf2_extension_set_tag(st, tag);
	fu_struct_uf2_extension_set_size(st, st->len + strlen(str));
	g_byte_array_append(st, (const guint8 *)str, strlen(str));
	fu_byte_array_align_up(st, FU_FIRMWARE_ALIGNMENT_4, 0x0);
	return g_steal_pointer(&st);
}

static GByteArray *
fu_uf2_firmware_write_chunk(FuUf2Firmware *self, FuChunk *chk, guint chk_len, GError **error)
{
	gsize offset_ext = FU_STRUCT_UF2_OFFSET_DATA + fu_chunk_get_data_sz(chk);
	guint32 addr = fu_firmware_get_addr(FU_FIRMWARE(self));
	guint32 flags = FU_UF2_FIRMWARE_BLOCK_FLAG_NONE;
	g_autoptr(GByteArray) st = fu_struct_uf2_new();
	g_autoptr(GPtrArray) extensions =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_struct_uf2_extension_unref);

	/* optional */
	if (fu_firmware_get_idx(FU_FIRMWARE(self)) > 0)
		flags |= FU_UF2_FIRMWARE_BLOCK_FLAG_HAS_FAMILY;

	/* build extensions */
	if (fu_firmware_get_idx(FU_FIRMWARE(self)) == 0x0) {
		if (fu_firmware_get_id(FU_FIRMWARE(self)) != NULL) {
			g_ptr_array_add(extensions,
					fu_uf2_firmware_build_utf8_extension(
					    FU_UF2_FIRMWARE_TAG_DESCRIPTION,
					    fu_firmware_get_id(FU_FIRMWARE(self))));
		}
		if (fu_firmware_get_version(FU_FIRMWARE(self)) != NULL) {
			g_ptr_array_add(extensions,
					fu_uf2_firmware_build_utf8_extension(
					    FU_UF2_FIRMWARE_TAG_VERSION,
					    fu_firmware_get_version(FU_FIRMWARE(self))));
		}
		if (extensions->len > 0) {
			g_ptr_array_add(extensions, fu_struct_uf2_extension_new());
			flags |= FU_UF2_FIRMWARE_BLOCK_FLAG_HAS_EXTENSION_TAG;
		}
	}

	/* offset from base address */
	addr += fu_chunk_get_idx(chk) * fu_chunk_get_data_sz(chk);

	/* build UF2 packet */
	fu_struct_uf2_set_flags(st, flags);
	fu_struct_uf2_set_target_addr(st, addr);
	fu_struct_uf2_set_payload_size(st, fu_chunk_get_data_sz(chk));
	fu_struct_uf2_set_block_no(st, fu_chunk_get_idx(chk));
	fu_struct_uf2_set_num_blocks(st, chk_len);
	fu_struct_uf2_set_family_id(st, fu_firmware_get_idx(FU_FIRMWARE(self)));
	if (!fu_struct_uf2_set_data(st, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk), error))
		return NULL;

	/* copy in any extensions */
	for (guint i = 0; i < extensions->len; i++) {
		FuStructUf2Extension *st_ext = g_ptr_array_index(extensions, i);
		if (!fu_memcpy_safe(st->data,
				    st->len,
				    offset_ext,
				    st_ext->data,
				    st_ext->len,
				    0x0,
				    st_ext->len,
				    error))
			return NULL;
		offset_ext += st_ext->len;
	}

	/* success */
	return g_steal_pointer(&st);
}

static GByteArray *
fu_uf2_firmware_write(FuFirmware *firmware, GError **error)
{
	FuUf2Firmware *self = FU_UF2_FIRMWARE(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* data first */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return NULL;

	/* write in chunks */
	chunks = fu_chunk_array_new_from_stream(stream,
						fu_firmware_get_addr(firmware),
						FU_CHUNK_PAGESZ_NONE,
						256,
						error);
	if (chunks == NULL)
		return NULL;
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) tmp = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return NULL;
		tmp = fu_uf2_firmware_write_chunk(self, chk, fu_chunk_array_length(chunks), error);
		if (tmp == NULL)
			return NULL;
		g_byte_array_append(buf, tmp->data, tmp->len);
	}

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_uf2_firmware_init(FuUf2Firmware *self)
{
}

static void
fu_uf2_firmware_class_init(FuUf2FirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_uf2_firmware_parse;
	firmware_class->write = fu_uf2_firmware_write;
}

FuFirmware *
fu_uf2_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_UF2_FIRMWARE, NULL));
}
