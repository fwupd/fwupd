/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuHidDevice"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-hid-report-item.h"
#include "fu-mem-private.h"
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
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuHidReportItem *self = FU_HID_REPORT_ITEM(firmware);
	const guint8 size_lookup[] = {0, 1, 2, 4};
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	guint8 val = buf[offset];
	guint8 data_size = size_lookup[val & 0b11];
	guint8 tag = (val & 0b11111100) >> 2;

	fu_firmware_set_idx(firmware, tag);
	fu_firmware_set_id(firmware, fu_hid_item_tag_to_string(tag));
	if (!fu_memchk_read(bufsz, offset, data_size + 1, error))
		return FALSE;
	if (tag == FU_HID_ITEM_TAG_LONG && data_size == 2) {
		if (offset + 1 >= bufsz) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_FAILED,
					    "not enough data to read long tag");
			return FALSE;
		}
		data_size = buf[++offset];
	} else {
		g_autoptr(GBytes) img = NULL;
		if (data_size == 1) {
			guint8 value = 0;
			if (!fu_memread_uint8_safe(buf, bufsz, offset + 1, &value, error))
				return FALSE;
			self->value = value;
		} else if (data_size == 2) {
			guint16 value = 0;
			if (!fu_memread_uint16_safe(buf,
						    bufsz,
						    offset + 1,
						    &value,
						    G_LITTLE_ENDIAN,
						    error))
				return FALSE;
			self->value = value;
		} else if (data_size == 4) {
			if (!fu_memread_uint32_safe(buf,
						    bufsz,
						    offset + 1,
						    &self->value,
						    G_LITTLE_ENDIAN,
						    error))
				return FALSE;
		}
		img = fu_bytes_new_offset(fw, offset + 1, data_size, error);
		if (img == NULL)
			return FALSE;
		fu_firmware_set_bytes(firmware, img);
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
	} else if (self->value <= 0xFF) {
		tmp |= 0b01;
		fu_byte_array_append_uint8(st, tmp);
		fu_byte_array_append_uint8(st, self->value);
	} else if (self->value <= 0xFFFF) {
		tmp |= 0b10;
		fu_byte_array_append_uint8(st, tmp);
		fu_byte_array_append_uint16(st, self->value, G_LITTLE_ENDIAN);
	} else if (self->value <= 0xFFFFFFFF) {
		tmp |= 0b11;
		fu_byte_array_append_uint8(st, tmp);
		fu_byte_array_append_uint32(st, self->value, G_LITTLE_ENDIAN);
	} else {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "value out of range");
		return NULL;
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
		if (!fu_strtoull(tmp, &value, 0x0, G_MAXUINT8, error))
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
		if (!fu_strtoull(tmp, &value, 0x0, G_MAXUINT32, error))
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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->export = fu_hid_report_item_export;
	klass_firmware->parse = fu_hid_report_item_parse;
	klass_firmware->write = fu_hid_report_item_write;
	klass_firmware->build = fu_hid_report_item_build;
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
