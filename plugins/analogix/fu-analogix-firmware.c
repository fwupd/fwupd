/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-firmware-common.h"
#include "fu-analogix-common.h"
#include "fu-analogix-firmware.h"

struct _FuAnalogixFirmware {
	FuIhexFirmwareClass parent_instance;
};

G_DEFINE_TYPE (FuAnalogixFirmware, fu_analogix_firmware, FU_TYPE_IHEX_FIRMWARE)

typedef struct {
	guint32 base_index;
	guint32 fw_max_addr;
	guint32 fw_start_addr;
	guint32 last_len;
	guint32 abs_addr;
	guint32 seg_addr;
} data_rcd_parser_ctx;

static void
fu_analogix_firmware_parse_ocm (FuIhexFirmwareRecord *rcd,
				data_rcd_parser_ctx *ctx,
				AnxImgHeader *img_header)
{
	img_header->fw_start_addr = FLASH_OCM_ADDR;
	ctx->fw_start_addr = FLASH_OCM_ADDR;
	ctx->fw_max_addr = FLASH_OCM_ADDR;
	ctx->last_len = rcd->byte_cnt;
	ctx->base_index = 0;
}

static void
fu_analogix_firmware_parse_txfw (FuIhexFirmwareRecord *rcd,
				 data_rcd_parser_ctx *ctx,
				 AnxImgHeader *img_header)
{
	img_header->secure_tx_start_addr = FLASH_TXFW_ADDR;
	img_header->fw_end_addr = ctx->fw_max_addr;
	if (ctx->fw_max_addr > ctx->fw_start_addr &&
	    img_header->fw_start_addr != 0) {
		img_header->fw_payload_len =
			ctx->fw_max_addr - ctx->fw_start_addr + ctx->last_len;
	}
	ctx->fw_start_addr = FLASH_TXFW_ADDR;
	ctx->fw_max_addr = FLASH_TXFW_ADDR;
	ctx->last_len = rcd->byte_cnt;
	ctx->base_index = img_header->fw_payload_len;
}

static void
fu_analogix_firmware_parse_rxfw (FuIhexFirmwareRecord *rcd,
				 data_rcd_parser_ctx *ctx,
				 AnxImgHeader *img_header)
{
	img_header->secure_rx_start_addr = FLASH_RXFW_ADDR;
	if (ctx->fw_max_addr > ctx->fw_start_addr &&
	    img_header->fw_start_addr > 0 &&
	    img_header->fw_payload_len == 0) {
		img_header->fw_payload_len =
			ctx->fw_max_addr - ctx->fw_start_addr + ctx->last_len;
	}

	if (ctx->fw_max_addr > ctx->fw_start_addr &&
	    img_header->secure_tx_start_addr > 0) {
		img_header->secure_tx_payload_len =
			ctx->fw_max_addr - ctx->fw_start_addr + ctx->last_len;
	}
	ctx->fw_start_addr = FLASH_RXFW_ADDR;
	ctx->fw_max_addr = FLASH_RXFW_ADDR;
	ctx->last_len = rcd->byte_cnt;
	ctx->base_index = img_header->secure_tx_payload_len + img_header->fw_payload_len;
}

static void
fu_analogix_firmware_parse_custom (FuIhexFirmwareRecord *rcd,
				   data_rcd_parser_ctx *ctx,
				   AnxImgHeader *img_header)
{
	img_header->custom_start_addr = FLASH_CUSTOM_ADDR;
	if (ctx->fw_max_addr > ctx->fw_start_addr &&
	    img_header->fw_start_addr > 0 &&
	    img_header->fw_payload_len == 0) {
		img_header->fw_payload_len =
			ctx->fw_max_addr - ctx->fw_start_addr + ctx->last_len;
	}
	if (ctx->fw_max_addr > ctx->fw_start_addr &&
	    img_header->secure_tx_start_addr > 0 &&
	    img_header->secure_tx_payload_len == 0) {
		img_header->secure_tx_payload_len =
			ctx->fw_max_addr - ctx->fw_start_addr + ctx->last_len;
	}

	if (ctx->fw_max_addr > ctx->fw_start_addr &&
	    img_header->secure_rx_start_addr > 0) {
		img_header->secure_rx_payload_len =
			ctx->fw_max_addr - ctx->fw_start_addr + ctx->last_len;
	}
	ctx->fw_start_addr = FLASH_CUSTOM_ADDR;
	ctx->fw_max_addr = FLASH_CUSTOM_ADDR;
	ctx->last_len = rcd->byte_cnt;
	ctx->base_index = img_header->secure_rx_payload_len +
		img_header->secure_tx_payload_len +
		img_header->fw_payload_len;
}

static gboolean
fu_analogix_firmware_parse_data_rcd (FuIhexFirmwareRecord *rcd,
				     data_rcd_parser_ctx *ctx,
				     AnxImgHeader *img_header,
				     GByteArray *payload_bytes,
				     GError **error)
{
	g_autoptr(GBytes) fw_hdr = NULL;
	guint32 addr = rcd->addr + ctx->seg_addr + ctx->abs_addr;
	guint32 version_addr = OCM_FW_VERSION_ADDR + ctx->seg_addr + ctx->abs_addr;

	switch (addr) {
	case FLASH_OCM_ADDR:
		fu_analogix_firmware_parse_ocm (rcd, ctx, img_header);
		break;
	case FLASH_TXFW_ADDR:
		fu_analogix_firmware_parse_txfw (rcd, ctx, img_header);
		break;
	case FLASH_RXFW_ADDR:
		fu_analogix_firmware_parse_rxfw (rcd, ctx, img_header);
		break;
	case FLASH_CUSTOM_ADDR:
		fu_analogix_firmware_parse_custom (rcd, ctx, img_header);
		break;
	default:
		break;
	}
	if (addr > ctx->fw_max_addr) {
		ctx->fw_max_addr = addr;
		ctx->last_len = rcd->byte_cnt;
	}
	g_byte_array_append (payload_bytes, rcd->data->data, rcd->data->len);
	if (addr == version_addr && ctx->fw_start_addr == FLASH_OCM_ADDR) {
		if (rcd->data->len == 16)
			img_header->fw_ver = rcd->data->data[8] << 8 | rcd->data->data[12];
	}

	return TRUE;
}

static gboolean
fu_analogix_firmware_parse (FuFirmware *firmware,
			    GBytes *fw,
			    guint64 addr_start,
			    guint64 addr_end,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuIhexFirmware *self = FU_IHEX_FIRMWARE (firmware);
	GPtrArray *records = fu_ihex_firmware_get_records (self);
	gboolean got_eof = FALSE;
	guint32 addr_last = 0x0;
	guint32 img_addr = G_MAXUINT32;
	g_autoptr(GBytes) hdr_bytes = NULL;
	g_autoptr(GBytes) payload_bytes = NULL;
	g_autoptr(GByteArray) payload = g_byte_array_new ();
	g_autoptr(FuFirmware) fw_hdr = fu_firmware_new ();
	g_autoptr(FuFirmware) fw_payload = fu_firmware_new ();
	data_rcd_parser_ctx ctx = {0};
	g_autofree gchar *version = NULL;
	AnxImgHeader *img_header = g_malloc0 (sizeof(*img_header));

	/* parse records */
	for (guint k = 0; k < records->len; k++) {
		FuIhexFirmwareRecord *rcd = g_ptr_array_index (records, k);
		guint16 addr16 = 0;
		guint32 addr = rcd->addr + ctx.seg_addr + ctx.abs_addr;
		guint32 len_hole;

		/* sanity check */
		if (rcd->record_type != FU_IHEX_FIRMWARE_RECORD_TYPE_EOF &&
		    rcd->data->len == 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "record 0x%x had zero size", k);
			return FALSE;
		}
		/* process different record types */
		switch (rcd->record_type) {
		case FU_IHEX_FIRMWARE_RECORD_TYPE_DATA:
			/* does not make sense */
			if (got_eof) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "cannot process data after EOF");
				return FALSE;
			}
			if (rcd->data->len == 0) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "cannot parse invalid data");
				return FALSE;
			}

			/* base address for element */
			if (img_addr == G_MAXUINT32)
				img_addr = addr;

			/* does not make sense */
			if (addr < addr_last) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "invalid address 0x%x, last was 0x%x on line %u",
					     (guint) addr,
					     (guint) addr_last,
					     rcd->ln);
				return FALSE;
			}

			/* any holes in the hex record */
			len_hole = addr - addr_last;
			if (addr_last > 0 && len_hole > 0x100000) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "hole of 0x%x bytes too large to fill on line %u",
					     (guint) len_hole,
					     rcd->ln);
				return FALSE;
			}
			if (addr_last > 0x0 && len_hole > 1) {
				g_debug ("filling address 0x%08x to 0x%08x on line %u",
					 addr_last + 1, addr_last + len_hole - 1, rcd->ln);
				for (guint j = 1; j < len_hole; j++) {
					/* although 0xff might be clearer,
					 * we can't write 0xffff to pic14 */
					fu_byte_array_append_uint8 (payload, 0x00);
				}
			}
			addr_last = addr + rcd->data->len - 1;
			if (addr_last < addr) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "overflow of address 0x%x on line %u",
					     (guint) addr, rcd->ln);
				return FALSE;
			}
			if (!fu_analogix_firmware_parse_data_rcd (rcd,
								  &ctx,
								  img_header,
								  payload,
								  error))
				return FALSE;
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_EOF:
			if (got_eof) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INVALID_FILE,
						     "duplicate EOF, perhaps "
						     "corrupt file");
				return FALSE;
			}
			got_eof = TRUE;
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_EXTENDED_LINEAR:
			if (!fu_common_read_uint16_safe (rcd->data->data, rcd->data->len,
							 0x0, &addr16, G_BIG_ENDIAN, error))
				return FALSE;
			ctx.abs_addr = (guint32) addr16 << 16;
			g_debug ("  abs_addr:\t0x%02x on line %u", ctx.abs_addr, rcd->ln);
			break;
		case FU_IHEX_FIRMWARE_RECORD_TYPE_EXTENDED_SEGMENT:
			if (!fu_common_read_uint16_safe (rcd->data->data, rcd->data->len,
							 0x0, &addr16, G_BIG_ENDIAN, error))
				return FALSE;
			/* segment base address, so ~1Mb addressable */
			ctx.seg_addr = (guint32) addr16 * 16;
			g_debug ("  seg_addr:\t0x%08x on line %u", ctx.seg_addr, rcd->ln);
			break;
		default:
			/* vendors sneak in nonstandard sections past the EOF */
			if (got_eof)
				break;
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid ihex record type %i on line %u",
				     rcd->record_type, rcd->ln);
			return FALSE;
		}
	}

	/* no EOF */
	if (!got_eof) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no EOF, perhaps truncated file");
		return FALSE;
	}

	/* only OCM */
	if (img_header->fw_payload_len == 0 && img_header->fw_start_addr != 0)
		img_header->fw_payload_len = ctx.fw_max_addr - ctx.fw_start_addr + ctx.last_len;
	if (img_header->secure_tx_start_addr != 0 && img_header->secure_tx_payload_len == 0)
		img_header->secure_tx_payload_len = ctx.fw_max_addr - ctx.fw_start_addr + ctx.last_len;
	if (img_header->secure_rx_start_addr != 0 && img_header->secure_rx_payload_len == 0)
		img_header->secure_rx_payload_len = ctx.fw_max_addr - ctx.fw_start_addr + ctx.last_len;
	if (img_header->custom_start_addr != 0 && img_header->custom_payload_len == 0)
		img_header->custom_payload_len = ctx.fw_max_addr - ctx.fw_start_addr + ctx.last_len;
	img_header->total_len = img_header->fw_payload_len +
		img_header->secure_tx_payload_len +
		img_header->secure_rx_payload_len +
		img_header->custom_payload_len;
	/* set firmware version */
	version = g_strdup_printf ("%04x.%04x", img_header->custom_ver,
								img_header->fw_ver);
	fu_firmware_set_version (firmware, version);

	/* add image header and payload */
	fu_firmware_set_id (fw_hdr, FU_FIRMWARE_ID_HEADER);
	hdr_bytes = g_bytes_new_take (img_header, sizeof(*img_header));
	fu_firmware_set_bytes (fw_hdr, hdr_bytes);
	fu_firmware_add_image (firmware, fw_hdr);

	fu_firmware_set_id (fw_payload, FU_FIRMWARE_ID_PAYLOAD);
	if (img_header->fw_start_addr != 0)
		fu_firmware_set_addr (fw_payload, img_header->fw_start_addr);
	else if (img_header->custom_start_addr != 0)
		fu_firmware_set_addr (fw_payload, img_header->custom_start_addr);
	payload_bytes = g_byte_array_free_to_bytes (payload);
	fu_firmware_set_bytes (fw_payload, payload_bytes);
	fu_firmware_add_image (firmware, fw_payload);

	return TRUE;
}

/*
 * Not used.
 */
/* static gboolean
fu_analogix_firmware_to_string (FuFirmware *firmware,
				GString *str)
{
	g_autoptr(GBytes) header_bytes = NULL;
	g_autoptr(GError) error = NULL;
	const AnxImgHeader *header = NULL;

	header_bytes = fu_firmware_get_image_by_id_bytes (firmware,
							  FU_FIRMWARE_ID_HEADER,
							  &error);
	if (header_bytes == NULL)
		return FALSE;
	header = (const AnxImgHeader *) g_bytes_get_data (header_bytes, NULL);
	if (header == NULL) {
		g_set_error (&error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "Error reading firmware header");
		return FALSE;
	}

	g_string_append_printf (str, "Total len: 0x%0x\n", header->total_len);
	g_string_append_printf (str, "OCM start: 0x%x, len:0x%x",
				header->fw_start_addr,
				header->fw_payload_len);
	g_string_append_printf (str, "Secure OCM TX start: 0x%x, len:0x%x",
				header->secure_tx_start_addr,
				header->secure_tx_payload_len);
	g_string_append_printf (str, "Secure OCM RX start: 0x%x, len:0x%x",
				header->secure_rx_start_addr,
				header->secure_rx_payload_len);
	g_string_append_printf (str, "Custom start: 0x%x, len:0x%x",
				header->custom_start_addr,
				header->custom_payload_len);

	return TRUE;
} */

static void
fu_analogix_firmware_init (FuAnalogixFirmware *self)
{
}

static void
fu_analogix_firmware_class_init (FuAnalogixFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_analogix_firmware_parse;
}

FuFirmware *
fu_analogix_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_ANALOGIX_FIRMWARE, NULL));
}
