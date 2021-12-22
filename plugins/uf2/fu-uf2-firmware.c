/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-uf2-firmware.h"

struct _FuUf2Firmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuUf2Firmware, fu_uf2_firmware, FU_TYPE_FIRMWARE)

#define FU_UF2_FIRMWARE_MAGIC_START0 0x0A324655u
#define FU_UF2_FIRMWARE_MAGIC_START1 0x9E5D5157u
#define FU_UF2_FIRMWARE_MAGIC_END    0x0AB16F30u

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
	guint32 magic = 0;
	guint32 flags = 0;
	guint32 addr = 0;
	guint32 datasz = 0;
	guint32 blockcnt = 0;
	guint32 blocktotal = 0;
	guint32 family_id = 0;

	/* sanity check */
	if (bufsz != 512) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "chunk size invalid, expected 512 bytes and got %u",
			    fu_chunk_get_data_sz(chk));
		return FALSE;
	}

	/* check magic */
	if (!fu_common_read_uint32_safe(buf, bufsz, 0x000, &magic, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (magic != FU_UF2_FIRMWARE_MAGIC_START0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "magic bytes #1 failed, expected 0x%08x bytes and got 0x%08x",
			    FU_UF2_FIRMWARE_MAGIC_START0,
			    magic);
		return FALSE;
	}
	if (!fu_common_read_uint32_safe(buf, bufsz, 0x004, &magic, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (magic != FU_UF2_FIRMWARE_MAGIC_START1) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "magic bytes #2 failed, expected 0x%08x bytes and got 0x%08x",
			    FU_UF2_FIRMWARE_MAGIC_START1,
			    magic);
		return FALSE;
	}
	if (!fu_common_read_uint32_safe(buf, bufsz, 0x008, &flags, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (flags & FU_UF2_FIRMWARE_BLOCK_FLAG_IS_CONTAINER) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "container U2F firmware not supported");
		return FALSE;
	}
	if (!fu_common_read_uint32_safe(buf, bufsz, 0x00C, &addr, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint32_safe(buf, bufsz, 0x010, &datasz, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (datasz > 476) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "data size impossible got 0x%08x",
			    datasz);
		return FALSE;
	}
	if (!fu_common_read_uint32_safe(buf, bufsz, 0x014, &blockcnt, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (blockcnt != fu_chunk_get_idx(chk)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "block count invalid, expected 0x%04x and got 0x%04x",
			    fu_chunk_get_idx(chk),
			    blockcnt);
		return FALSE;
	}
	if (!fu_common_read_uint32_safe(buf, bufsz, 0x018, &blocktotal, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (blocktotal == 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "block count invalid, expected > 0");
		return FALSE;
	}
	if (!fu_common_read_uint32_safe(buf, bufsz, 0x01C, &family_id, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (flags & FU_UF2_FIRMWARE_BLOCK_FLAG_HAS_FAMILY) {
		if (family_id == 0) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "family_id required but not supplied");
			return FALSE;
		}
	}

	/* assume first chunk is representative of firmware */
	if (fu_chunk_get_idx(chk) == 0) {
		fu_firmware_set_addr(FU_FIRMWARE(self), addr);
		fu_firmware_set_idx(FU_FIRMWARE(self), family_id);
	}

	/* just append raw data */
	g_byte_array_append(tmp, buf + 0x020, datasz);
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
		gsize offset = 0x20 + datasz;
		while (offset < bufsz) {
			guint8 sz = 0;
			guint32 tag = 0;

			/* [SZ][TAG][TAG][TAG][TAG][DATA....] */
			if (!fu_common_read_uint8_safe(buf, bufsz, offset, &sz, error))
				return FALSE;
			if (sz < 4) {
				g_set_error_literal(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "invalid extension tag size");
				return FALSE;
			}
			if (!fu_common_read_uint32_safe(buf,
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
				g_warning("unknown tag 0x%06x", tag);
			}

			/* next! */
			offset += sz;
		}
	}
	if (!fu_common_read_uint32_safe(buf, bufsz, 0x1FC, &magic, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (magic != FU_UF2_FIRMWARE_MAGIC_END) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "magic bytes #3 failed, expected 0x%08x bytes and got 0x%08x",
			    FU_UF2_FIRMWARE_MAGIC_END,
			    magic);
		return FALSE;
	}

	/* dump */
	if (g_getenv("FWUPD_U2F_VERBOSE") != NULL) {
		g_debug("block: 0x%x/0x%x @0x%x", blockcnt, blocktotal - 1, addr);
		g_debug("family_id: 0x%x", family_id);
		g_debug("flags: 0x%x", flags);
		g_debug("datasz: 0x%x", datasz);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uf2_firmware_parse(FuFirmware *firmware,
		      GBytes *fw,
		      guint64 addr_start,
		      guint64 addr_end,
		      FwupdInstallFlags flags,
		      GError **error)
{
	FuUf2Firmware *self = FU_UF2_FIRMWARE(firmware);
	g_autoptr(GByteArray) tmp = g_byte_array_new();
	g_autoptr(GPtrArray) chunks = NULL;

	/* read in fixed sized chunks */
	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, 512);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_uf2_firmware_parse_chunk(self, chk, tmp, error))
			return FALSE;
	}

	/* success */
	fu_firmware_set_bytes(firmware, g_byte_array_free_to_bytes(g_steal_pointer(&tmp)));
	return TRUE;
}

static GByteArray *
fu_uf2_firmware_write_chunk(FuUf2Firmware *self, FuChunk *chk, guint chk_len, GError **error)
{
	guint32 addr = fu_firmware_get_addr(FU_FIRMWARE(self));
	guint32 family_id = fu_firmware_get_idx(FU_FIRMWARE(self));
	guint32 flags = FU_UF2_FIRMWARE_BLOCK_FLAG_NONE;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) datapad = g_byte_array_new();

	/* sanity check */
	if (fu_chunk_get_data_sz(chk) > 476) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "chunk size invalid, expected < 476 bytes and got %u",
			    fu_chunk_get_data_sz(chk));
		return NULL;
	}

	/* pad out data */
	g_byte_array_append(datapad, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
	fu_byte_array_set_size_full(datapad, 476, 0x0);

	/* optional */
	if (family_id > 0)
		flags |= FU_UF2_FIRMWARE_BLOCK_FLAG_HAS_FAMILY;

	/* offset from base address */
	addr += fu_chunk_get_idx(chk) * fu_chunk_get_data_sz(chk);

	/* build UF2 packet */
	fu_byte_array_append_uint32(buf, FU_UF2_FIRMWARE_MAGIC_START0, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, FU_UF2_FIRMWARE_MAGIC_START1, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, flags, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, addr, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, fu_chunk_get_data_sz(chk), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, fu_chunk_get_idx(chk), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, chk_len, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, family_id, G_LITTLE_ENDIAN);
	g_byte_array_append(buf, datapad->data, datapad->len);
	fu_byte_array_append_uint32(buf, FU_UF2_FIRMWARE_MAGIC_END, G_LITTLE_ENDIAN);

	/* success */
	return g_steal_pointer(&buf);
}

static GBytes *
fu_uf2_firmware_write(FuFirmware *firmware, GError **error)
{
	FuUf2Firmware *self = FU_UF2_FIRMWARE(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* data first */
	fw = fu_firmware_get_bytes(firmware, error);
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
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
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
