/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-uf2-firmware.h"
#include "fu-uf2-struct.h"

struct _FuUf2Firmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuUf2Firmware, fu_uf2_firmware, FU_TYPE_FIRMWARE)

#define FU_UF2_FIRMWARE_BLOCK_FLAG_NONE		     0x00000000
#define FU_UF2_FIRMWARE_BLOCK_FLAG_NOFLASH	     0x00000001
#define FU_UF2_FIRMWARE_BLOCK_FLAG_IS_CONTAINER	     0x00001000
#define FU_UF2_FIRMWARE_BLOCK_FLAG_HAS_FAMILY	     0x00002000
#define FU_UF2_FIRMWARE_BLOCK_FLAG_HAS_MD5	     0x00004000
#define FU_UF2_FIRMWARE_BLOCK_FLAG_HAS_EXTENSION_TAG 0x00008000

#define FU_U2F_FIRMWARE_TAG_VERSION	0x9fc7bc /* semver of firmware file (UTF-8) */
#define FU_U2F_FIRMWARE_TAG_DESCRIPTION 0x650d9d /* description of device (UTF-8) */
#define FU_U2F_FIRMWARE_TAG_PAGE_SZ	0x0be9f7 /* page size of target device (uint32_t) */
#define FU_U2F_FIRMWARE_TAG_SHA1	0xb46db0 /* SHA-2 checksum of firmware */
#define FU_U2F_FIRMWARE_TAG_DEVICE_ID	0xc8a729 /* device type identifier (uint32_t or uint64_t) */

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
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "container U2F firmware not supported");
		return FALSE;
	}
	datasz = fu_struct_uf2_get_payload_size(st);
	if (datasz > 476) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "data size impossible got 0x%08x",
			    datasz);
		return FALSE;
	}
	if (fu_struct_uf2_get_block_no(st) != fu_chunk_get_idx(chk)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "block count invalid, expected 0x%04x and got 0x%04x",
			    fu_chunk_get_idx(chk),
			    fu_struct_uf2_get_block_no(st));
		return FALSE;
	}
	if (fu_struct_uf2_get_num_blocks(st) == 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "block count invalid, expected > 0");
		return FALSE;
	}
	if (flags & FU_UF2_FIRMWARE_BLOCK_FLAG_HAS_FAMILY) {
		if (fu_struct_uf2_get_family_id(st) == 0) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
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
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "not enough space for MD5 checksum");
			return FALSE;
		}
	}
	if (flags & FU_UF2_FIRMWARE_BLOCK_FLAG_HAS_EXTENSION_TAG) {
		gsize offset = FU_STRUCT_UF2_OFFSET_DATA;
		while (offset < bufsz) {
			guint8 sz = 0;
			guint32 tag = 0;

			/* [SZ][TAG][TAG][TAG][TAG][DATA....] */
			if (!fu_memread_uint8_safe(buf, bufsz, offset, &sz, error))
				return FALSE;
			if (sz < 4) {
				g_set_error_literal(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "invalid extension tag size");
				return FALSE;
			}
			if (!fu_memread_uint32_safe(buf,
						    bufsz,
						    offset,
						    &tag,
						    G_LITTLE_ENDIAN,
						    error))
				return FALSE;
			tag &= 0xFFFFFF;
			if (tag == FU_U2F_FIRMWARE_TAG_VERSION) {
				g_autofree gchar *utf8buf = g_malloc0(sz);
				if (!fu_memcpy_safe((guint8 *)utf8buf,
						    sz,
						    0x0, /* dst */
						    buf,
						    bufsz,
						    offset + 0x4, /* src */
						    sz - 4,
						    error))
					return FALSE;
				fu_firmware_set_version(FU_FIRMWARE(self), utf8buf);
			} else if (tag == FU_U2F_FIRMWARE_TAG_DESCRIPTION) {
				g_autofree gchar *utf8buf = g_malloc0(sz);
				if (!fu_memcpy_safe((guint8 *)utf8buf,
						    sz,
						    0x0, /* dst */
						    buf,
						    bufsz,
						    offset + 0x4, /* src */
						    sz - 4,
						    error))
					return FALSE;
				fu_firmware_set_id(FU_FIRMWARE(self), utf8buf);
			} else {
				if (g_getenv("FWUPD_FUZZER_RUNNING") == NULL)
					g_warning("unknown tag 0x%06x", tag);
			}

			/* next! */
			offset += sz;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uf2_firmware_parse(FuFirmware *firmware,
		      GBytes *fw,
		      gsize offset,
		      FwupdInstallFlags flags,
		      GError **error)
{
	FuUf2Firmware *self = FU_UF2_FIRMWARE(firmware);
	g_autoptr(GByteArray) tmp = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* read in fixed sized chunks */
	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, 512);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_uf2_firmware_parse_chunk(self, chk, tmp, error))
			return FALSE;
	}

	/* success */
	blob = g_bytes_new(tmp->data, tmp->len);
	fu_firmware_set_bytes(firmware, blob);
	return TRUE;
}

static GByteArray *
fu_uf2_firmware_write_chunk(FuUf2Firmware *self, FuChunk *chk, guint chk_len, GError **error)
{
	guint32 addr = fu_firmware_get_addr(FU_FIRMWARE(self));
	guint32 flags = FU_UF2_FIRMWARE_BLOCK_FLAG_NONE;
	g_autoptr(GByteArray) st = fu_struct_uf2_new();

	/* optional */
	if (fu_firmware_get_idx(FU_FIRMWARE(self)) > 0)
		flags |= FU_UF2_FIRMWARE_BLOCK_FLAG_HAS_FAMILY;

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

	/* success */
	return g_steal_pointer(&st);
}

static GByteArray *
fu_uf2_firmware_write(FuFirmware *firmware, GError **error)
{
	FuUf2Firmware *self = FU_UF2_FIRMWARE(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* data first */
	fw = fu_firmware_get_bytes_with_patches(firmware, error);
	if (fw == NULL)
		return NULL;

	/* write in chunks */
	chunks = fu_chunk_array_new_from_bytes(fw, fu_firmware_get_addr(firmware), 0x0, 256);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) tmp = NULL;
		tmp = fu_uf2_firmware_write_chunk(self, chk, chunks->len, error);
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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_uf2_firmware_parse;
	klass_firmware->write = fu_uf2_firmware_write;
}

FuFirmware *
fu_uf2_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_UF2_FIRMWARE, NULL));
}
