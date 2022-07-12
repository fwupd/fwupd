/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-wac-firmware.h"

struct _FuWacFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuWacFirmware, fu_wac_firmware, FU_TYPE_FIRMWARE)

#define FU_WAC_FIRMWARE_TOKENS_MAX   100000 /* lines */
#define FU_WAC_FIRMWARE_SECTIONS_MAX 10

typedef struct {
	guint32 addr;
	guint32 sz;
	guint32 prog_start_addr;
} FuFirmwareWacHeaderRecord;

typedef struct {
	FuFirmware *firmware;
	FwupdInstallFlags flags;
	GPtrArray *header_infos;
	GString *image_buffer;
	guint8 images_cnt;
} FuWacFirmwareTokenHelper;

static gboolean
fu_wac_firmware_tokenize_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	FuWacFirmwareTokenHelper *helper = (FuWacFirmwareTokenHelper *)user_data;
	g_autofree gchar *cmd = NULL;

	/* sanity check */
	if (token_idx > FU_WAC_FIRMWARE_TOKENS_MAX) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "file has too many lines");
		return FALSE;
	}

	/* remove WIN32 line endings */
	g_strdelimit(token->str, "\r\x1a", '\0');
	token->len = strlen(token->str);

	/* ignore blank lines */
	cmd = g_strndup(token->str, 2);
	if (g_strcmp0(cmd, "") == 0)
		return TRUE;

	/* Wacom-specific metadata */
	if (g_strcmp0(cmd, "WA") == 0) {
		/* header info record */
		if (token->len > 3 && memcmp(token->str + 2, "COM", 3) == 0) {
			guint8 header_image_cnt = 0;
			if (token->len != 40) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "invalid header, got %" G_GSIZE_FORMAT " bytes",
					    token->len);
				return FALSE;
			}

			/* sanity check */
			if (helper->header_infos->len > FU_WAC_FIRMWARE_SECTIONS_MAX) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "too many metadata sections: %u",
					    helper->header_infos->len);
				return FALSE;
			}
			if (!fu_firmware_strparse_uint4_safe(token->str,
							     token->len,
							     5,
							     &header_image_cnt,
							     error))
				return FALSE;
			for (guint j = 0; j < header_image_cnt; j++) {
				g_autofree FuFirmwareWacHeaderRecord *hdr = NULL;
				hdr = g_new0(FuFirmwareWacHeaderRecord, 1);
				if (!fu_firmware_strparse_uint32_safe(token->str,
								      token->len,
								      (j * 16) + 6,
								      &hdr->addr,
								      error))
					return FALSE;
				if (!fu_firmware_strparse_uint32_safe(token->str,
								      token->len,
								      (j * 16) + 14,
								      &hdr->sz,
								      error))
					return FALSE;
				g_debug("header_fw%u_addr: 0x%x", j, hdr->addr);
				g_debug("header_fw%u_sz:   0x%x", j, hdr->sz);
				g_ptr_array_add(helper->header_infos, g_steal_pointer(&hdr));
			}
			return TRUE;
		}

		/* firmware headline record */
		if (token->len == 13) {
			FuFirmwareWacHeaderRecord *hdr;
			guint8 idx = 0;
			if (!fu_firmware_strparse_uint4_safe(token->str,
							     token->len,
							     2,
							     &idx,
							     error))
				return FALSE;
			if (idx == 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "headline %u invalid",
					    idx);
				return FALSE;
			}
			if (idx > helper->header_infos->len) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "headline %u exceeds header count %u",
					    idx,
					    helper->header_infos->len);
				return FALSE;
			}
			if (idx - 1 != helper->images_cnt) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "headline %u is not in sorted order",
					    idx);
				return FALSE;
			}
			hdr = g_ptr_array_index(helper->header_infos, idx - 1);
			if (!fu_firmware_strparse_uint32_safe(token->str,
							      token->len,
							      3,
							      &hdr->prog_start_addr,
							      error))
				return FALSE;
			if (hdr->prog_start_addr != hdr->addr) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "programming address 0x%x != "
					    "base address 0x%0x for idx %u",
					    hdr->prog_start_addr,
					    hdr->addr,
					    idx);
				return FALSE;
			}
			g_debug("programing-start-address: 0x%x", hdr->prog_start_addr);
			return TRUE;
		}

		g_debug("unknown Wacom-specific metadata");
		return TRUE;
	}

	/* start */
	if (g_strcmp0(cmd, "S0") == 0) {
		if (helper->image_buffer->len > 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "duplicate S0 without S7");
			return FALSE;
		}
		g_string_append_printf(helper->image_buffer, "%s\n", token->str);
		return TRUE;
	}

	/* these are things we want to include in the image */
	if (g_strcmp0(cmd, "S1") == 0 || g_strcmp0(cmd, "S2") == 0 || g_strcmp0(cmd, "S3") == 0 ||
	    g_strcmp0(cmd, "S5") == 0 || g_strcmp0(cmd, "S7") == 0 || g_strcmp0(cmd, "S8") == 0 ||
	    g_strcmp0(cmd, "S9") == 0) {
		if (helper->image_buffer->len == 0) {
			g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "%s without S0", cmd);
			return FALSE;
		}
		g_string_append_printf(helper->image_buffer, "%s\n", token->str);
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid SREC command on line %u: %s",
			    token_idx + 1,
			    cmd);
		return FALSE;
	}

	/* end */
	if (g_strcmp0(cmd, "S7") == 0) {
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(GBytes) fw_srec = NULL;
		g_autoptr(FuFirmware) firmware_srec = fu_srec_firmware_new();
		g_autoptr(FuFirmware) img = fu_firmware_new();
		FuFirmwareWacHeaderRecord *hdr;

		/* get the correct relocated start address */
		if (helper->images_cnt >= helper->header_infos->len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "%s without header",
				    cmd);
			return FALSE;
		}
		hdr = g_ptr_array_index(helper->header_infos, helper->images_cnt);

		if (helper->image_buffer->len == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "%s with missing image buffer",
				    cmd);
			return FALSE;
		}

		/* parse SREC file and add as image */
		blob = g_bytes_new(helper->image_buffer->str, helper->image_buffer->len);
		if (!fu_firmware_parse_full(firmware_srec,
					    blob,
					    hdr->addr,
					    helper->flags | FWUPD_INSTALL_FLAG_NO_SEARCH,
					    error))
			return FALSE;
		fw_srec = fu_firmware_get_bytes(firmware_srec, error);
		if (fw_srec == NULL)
			return FALSE;
		fu_firmware_set_bytes(img, fw_srec);
		fu_firmware_set_addr(img, fu_firmware_get_addr(firmware_srec));
		fu_firmware_set_idx(img, helper->images_cnt);
		fu_firmware_add_image(helper->firmware, img);
		helper->images_cnt++;

		/* clear the image buffer */
		g_string_set_size(helper->image_buffer, 0);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wac_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	guint8 magic[5] = {0x0};

	if (!fu_memcpy_safe(magic,
			    sizeof(magic),
			    0, /* dst */
			    g_bytes_get_data(fw, NULL),
			    g_bytes_get_size(fw),
			    offset,
			    sizeof(magic),
			    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	if (memcmp(magic, "WACOM", sizeof(magic)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid .wac prefix");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wac_firmware_parse(FuFirmware *firmware,
		      GBytes *fw,
		      gsize offset,
		      FwupdInstallFlags flags,
		      GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GPtrArray) header_infos = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GString) image_buffer = g_string_new(NULL);
	FuWacFirmwareTokenHelper helper = {.firmware = firmware,
					   .flags = flags,
					   .header_infos = header_infos,
					   .image_buffer = image_buffer,
					   .images_cnt = 0};

	/* tokenize */
	if (!fu_strsplit_full((const gchar *)buf + offset,
			      bufsz - offset,
			      "\n",
			      fu_wac_firmware_tokenize_cb,
			      &helper,
			      error))
		return FALSE;

	/* verify data is complete */
	if (helper.image_buffer->len > 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "truncated data: no S7");
		return FALSE;
	}

	/* ensure this matched the header */
	if (helper.header_infos->len != helper.images_cnt) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "not enough images %u for header count %u",
			    helper.images_cnt,
			    header_infos->len);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static guint8
fu_wac_firmware_calc_checksum(GByteArray *buf)
{
	return fu_sum8(buf->data, buf->len) ^ 0xFF;
}

static GBytes *
fu_wac_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	g_autoptr(GString) str = g_string_new(NULL);
	g_autoptr(GByteArray) buf_hdr = g_byte_array_new();

	/* fw header */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		fu_byte_array_append_uint32(buf_hdr, fu_firmware_get_addr(img), G_BIG_ENDIAN);
		fu_byte_array_append_uint32(buf_hdr, fu_firmware_get_size(img), G_BIG_ENDIAN);
	}
	g_string_append_printf(str, "WACOM%u", images->len);
	for (guint i = 0; i < buf_hdr->len; i++)
		g_string_append_printf(str, "%02X", buf_hdr->data[i]);
	g_string_append_printf(str, "%02X\n", fu_wac_firmware_calc_checksum(buf_hdr));

	/* payload */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) img_blob = NULL;
		g_autoptr(GByteArray) buf_img = g_byte_array_new();

		/* img header */
		g_string_append_printf(str, "WA%u", (guint)fu_firmware_get_idx(img) + 1);
		fu_byte_array_append_uint32(buf_img, fu_firmware_get_addr(img), G_BIG_ENDIAN);
		for (guint j = 0; j < buf_img->len; j++)
			g_string_append_printf(str, "%02X", buf_img->data[j]);
		g_string_append_printf(str, "%02X\n", fu_wac_firmware_calc_checksum(buf_img));

		/* srec */
		img_blob = fu_firmware_write(img, error);
		if (img_blob == NULL)
			return NULL;
		g_string_append_len(str,
				    (const gchar *)g_bytes_get_data(img_blob, NULL),
				    g_bytes_get_size(img_blob));
	}

	/* success */
	return g_string_free_to_bytes(g_steal_pointer(&str));
}

static void
fu_wac_firmware_init(FuWacFirmware *self)
{
}

static void
fu_wac_firmware_class_init(FuWacFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_wac_firmware_check_magic;
	klass_firmware->parse = fu_wac_firmware_parse;
	klass_firmware->write = fu_wac_firmware_write;
}

FuFirmware *
fu_wac_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_WAC_FIRMWARE, NULL));
}
