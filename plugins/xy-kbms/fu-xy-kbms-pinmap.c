/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-xy-kbms-common.h"
#include "fu-xy-kbms-pinmap.h"
#include "fu-xy-kbms-struct.h"

struct _FuXyKbmsPinmap {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuXyKbmsPinmap, fu_xy_kbms_pinmap, FU_TYPE_FIRMWARE)

#define FU_XY_KBMS_PINMAP_HEX_DIGITS_MAX 5

static const guint8 fu_xy_kbms_pinmap_sentinel[] = {0xAA, 0xBB, 0xCC, 0xDD};

/*
 * Each "0xNN" token is one byte and each "0xNNNN" token is a little-endian word; anything after
 * ';' on a line is a comment. The text may be GBK encoded, so treat it as bytes and not UTF-8.
 */
static gboolean
fu_xy_kbms_pinmap_parse_text(GByteArray *buf, const gchar *text, gsize textsz, GError **error)
{
	guint lineno = 1;

	for (gsize i = 0; i < textsz; i++) {
		guint64 value = 0;
		gchar digits[FU_XY_KBMS_PINMAP_HEX_DIGITS_MAX + 1] = {'\0'};
		gsize ndigits = 0;

		if (text[i] == '\n') {
			lineno++;
			continue;
		}
		if (text[i] == ';') {
			while (i + 1 < textsz && text[i + 1] != '\n')
				i++;
			continue;
		}
		if (text[i] != '0' || i + 1 >= textsz || (text[i + 1] != 'x' && text[i + 1] != 'X'))
			continue;
		i += 2;
		while (i < textsz && g_ascii_isxdigit(text[i]) &&
		       ndigits < FU_XY_KBMS_PINMAP_HEX_DIGITS_MAX)
			digits[ndigits++] = text[i++];
		i--;
		if (ndigits != 2 && ndigits != 4) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "line %u: hex value must have 2 or 4 digits",
				    lineno);
			return FALSE;
		}
		if (!fu_strtoull(digits, &value, 0x0, G_MAXUINT16, FU_INTEGER_BASE_16, error)) {
			g_prefix_error(error, "line %u: ", lineno);
			return FALSE;
		}
		if (ndigits == 2)
			fu_byte_array_append_uint8(buf, value);
		else
			fu_byte_array_append_uint16(buf, value, G_LITTLE_ENDIAN);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_xy_kbms_pinmap_parse(FuFirmware *firmware,
			FuInputStream *stream,
			FuFirmwareParseFlags flags,
			GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) fw = NULL;

	blob = fu_input_stream_read_bytes(stream, 0x0, G_MAXSIZE, NULL, error);
	if (blob == NULL)
		return FALSE;
	if (!fu_xy_kbms_pinmap_parse_text(buf,
					  g_bytes_get_data(blob, NULL),
					  g_bytes_get_size(blob),
					  error))
		return FALSE;
	if (buf->len != FU_STRUCT_XY_KBMS_CONFIG_SIZE_PINMAP) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "pinmap has 0x%x bytes, expected 0x%x",
			    buf->len,
			    (guint)FU_STRUCT_XY_KBMS_CONFIG_SIZE_PINMAP);
		return FALSE;
	}
	if (!fu_memcmp_safe(buf->data,
			    buf->len,
			    FU_XY_KBMS_PINMAP_SENTINEL_OFFSET,
			    fu_xy_kbms_pinmap_sentinel,
			    sizeof(fu_xy_kbms_pinmap_sentinel),
			    0x0,
			    sizeof(fu_xy_kbms_pinmap_sentinel),
			    error)) {
		g_prefix_error_literal(error, "pinmap is missing the 0xaa 0xbb 0xcc 0xdd marker: ");
		return FALSE;
	}
	if (buf->data[buf->len - 1] != FU_XY_KBMS_PINMAP_VERSION) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "pinmap version %u not supported",
			    buf->data[buf->len - 1]);
		return FALSE;
	}

	/* success */
	fw = g_bytes_new(buf->data, buf->len);
	fu_firmware_set_bytes(firmware, fw);
	return TRUE;
}

static GByteArray *
fu_xy_kbms_pinmap_write(FuFirmware *firmware, GError **error)
{
	const guint8 *buf;
	gsize bufsz = 0;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GByteArray) str = g_byte_array_new();

	/* one byte per line, which parses back to the same pinmap */
	fw = fu_firmware_get_bytes_with_patches(firmware, error);
	if (fw == NULL)
		return NULL;
	buf = g_bytes_get_data(fw, &bufsz);
	for (gsize i = 0; i < bufsz; i++) {
		g_autofree gchar *tmp = g_strdup_printf("0x%02x\n", buf[i]);
		g_byte_array_append(str, (const guint8 *)tmp, strlen(tmp));
	}
	return g_steal_pointer(&str);
}

static void
fu_xy_kbms_pinmap_init(FuXyKbmsPinmap *self)
{
}

static void
fu_xy_kbms_pinmap_class_init(FuXyKbmsPinmapClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_set_size_max(firmware_class, 128 * FU_KB);
	firmware_class->parse = fu_xy_kbms_pinmap_parse;
	firmware_class->write = fu_xy_kbms_pinmap_write;
}

FuFirmware *
fu_xy_kbms_pinmap_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_XY_KBMS_PINMAP, NULL));
}
