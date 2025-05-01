/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuHidDevice"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-hid-report-item.h"
#include "fu-input-stream.h"
#include "fu-mem-private.h"
#include "fu-partial-input-stream.h"
#include "fu-string.h"

/**
 * FuHidReportItem:
 *
 * See also: [class@FuHidDescriptor]
 */

struct _FuHidReportItem {
	FuFirmware parent_instance;
	guint32 value;
};

G_DEFINE_TYPE(FuHidReportItem, fu_hid_report_item, FU_TYPE_FIRMWARE)

FuHidItemKind
fu_hid_report_item_get_kind(FuHidReportItem *self)
{
	g_return_val_if_fail(FU_IS_HID_REPORT_ITEM(self), 0);
	return fu_firmware_get_idx(FU_FIRMWARE(self)) & 0b11;
}

guint32
fu_hid_report_item_get_value(FuHidReportItem *self)
{
	g_return_val_if_fail(FU_IS_HID_REPORT_ITEM(self), 0);
	return self->value;
}

static void
fu_hid_report_item_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuHidReportItem *self = FU_HID_REPORT_ITEM(firmware);
	fu_xmlb_builder_insert_kv(bn,
				  "kind",
				  fu_hid_item_kind_to_string(fu_hid_report_item_get_kind(self)));
	fu_xmlb_builder_insert_kx(bn, "value", self->value);
}

static gboolean
fu_hid_report_item_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	FuHidReportItem *self = FU_HID_REPORT_ITEM(firmware);
	const guint8 size_lookup[] = {0, 1, 2, 4};
	guint8 data_size;
	guint8 tag;
	guint8 val = 0;

	if (!fu_input_stream_read_u8(stream, 0x0, &val, error))
		return FALSE;
	data_size = size_lookup[val & 0b11];
	tag = (val & 0b11111100) >> 2;
	fu_firmware_set_idx(firmware, tag);
	fu_firmware_set_id(firmware, fu_hid_item_tag_to_string(tag));

	if (tag == FU_HID_ITEM_TAG_LONG && data_size == 2) {
		gsize streamsz = 0;
		if (!fu_input_stream_size(stream, &streamsz, error))
			return FALSE;
		if (streamsz < 1) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "not enough data to read long tag");
			return FALSE;
		}
		if (!fu_input_stream_read_u8(stream, 1, &data_size, error))
			return FALSE;
	} else {
		g_autoptr(GInputStream) partial_stream = NULL;
		if (data_size == 1) {
			guint8 value = 0;
			if (!fu_input_stream_read_u8(stream, 1, &value, error))
				return FALSE;
			self->value = value;
		} else if (data_size == 2) {
			guint16 value = 0;
			if (!fu_input_stream_read_u16(stream, 1, &value, G_LITTLE_ENDIAN, error))
				return FALSE;
			self->value = value;
		} else if (data_size == 4) {
			if (!fu_input_stream_read_u32(stream,
						      1,
						      &self->value,
						      G_LITTLE_ENDIAN,
						      error))
				return FALSE;
		}
		partial_stream = fu_partial_input_stream_new(stream, 1, data_size, error);
		if (partial_stream == NULL) {
			g_prefix_error(error, "failed to cut HID payload: ");
			return FALSE;
		}
		if (!fu_firmware_set_stream(firmware, partial_stream, error))
			return FALSE;
	}

	/* success */
	fu_firmware_set_size(firmware, 1 + data_size);
	return TRUE;
}

static GByteArray *
fu_hid_report_item_write(FuFirmware *firmware, GError **error)
{
	FuHidReportItem *self = FU_HID_REPORT_ITEM(firmware);
	g_autoptr(GByteArray) st = g_byte_array_new();
	guint8 tmp = fu_firmware_get_idx(firmware) << 2;

	if (self->value == 0) {
		fu_byte_array_append_uint8(st, tmp);
	} else if (self->value <= G_MAXUINT8) {
		tmp |= 0b01;
		fu_byte_array_append_uint8(st, tmp);
		fu_byte_array_append_uint8(st, self->value);
	} else if (self->value <= G_MAXUINT16) {
		tmp |= 0b10;
		fu_byte_array_append_uint8(st, tmp);
		fu_byte_array_append_uint16(st, self->value, G_LITTLE_ENDIAN);
	} else {
		tmp |= 0b11;
		fu_byte_array_append_uint8(st, tmp);
		fu_byte_array_append_uint32(st, self->value, G_LITTLE_ENDIAN);
	}

	/* success */
	return g_steal_pointer(&st);
}

static gboolean
fu_hid_report_item_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuHidReportItem *self = FU_HID_REPORT_ITEM(firmware);
	const gchar *tmp;
	guint64 value = 0;

	/* optional data */
	tmp = xb_node_query_text(n, "idx", NULL);
	if (tmp != NULL) {
		if (!fu_strtoull(tmp, &value, 0x0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		fu_firmware_set_idx(firmware, value);
		fu_firmware_set_id(firmware, fu_hid_item_tag_to_string(value));
	}
	tmp = xb_node_query_text(n, "id", NULL);
	if (tmp != NULL) {
		fu_firmware_set_id(firmware, tmp);
		fu_firmware_set_idx(firmware, fu_hid_item_tag_from_string(tmp));
	}
	tmp = xb_node_query_text(n, "value", NULL);
	if (tmp != NULL) {
		if (!fu_strtoull(tmp, &value, 0x0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->value = value;
	}

	/* success */
	return TRUE;
}

static void
fu_hid_report_item_init(FuHidReportItem *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_hid_report_item_class_init(FuHidReportItemClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->export = fu_hid_report_item_export;
	firmware_class->parse = fu_hid_report_item_parse;
	firmware_class->write = fu_hid_report_item_write;
	firmware_class->build = fu_hid_report_item_build;
}

/**
 * fu_hid_report_item_new:
 *
 * Creates a new HID report item
 *
 * Returns: (transfer full): a #FuHidReportItem
 *
 * Since: 1.9.4
 **/
FuHidReportItem *
fu_hid_report_item_new(void)
{
	return g_object_new(FU_TYPE_HID_REPORT_ITEM, NULL);
}
