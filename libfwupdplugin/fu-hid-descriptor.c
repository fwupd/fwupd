/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuHidDevice"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-hid-descriptor.h"
#include "fu-hid-report-item.h"
#include "fu-hid-struct.h"
#include "fu-mem.h"

/**
 * FuHidDescriptor:
 *
 * A HID descriptor.
 *
 * Each report is a image of this firmware object and each report has children of #FuHidReportItem.
 *
 * Documented: https://www.usb.org/sites/default/files/hid1_11.pdf
 *
 * See also: [class@FuFirmware]
 */

G_DEFINE_TYPE(FuHidDescriptor, fu_hid_descriptor, FU_TYPE_FIRMWARE)

static gboolean
fu_hid_descriptor_parse(FuFirmware *firmware,
			GBytes *fw,
			gsize offset,
			FwupdInstallFlags flags,
			GError **error)
{
	g_autoptr(GPtrArray) table_state =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	while (offset < g_bytes_get_size(fw)) {
		g_autofree gchar *itemstr = NULL;
		g_autoptr(FuHidReportItem) item = fu_hid_report_item_new();

		if (!fu_firmware_parse_full(FU_FIRMWARE(item), fw, offset, flags, error))
			return FALSE;
		offset += fu_firmware_get_size(FU_FIRMWARE(item));

		/* only for debugging */
		itemstr = fu_firmware_to_string(FU_FIRMWARE(item));
		g_debug("add to table-state:\n%s", itemstr);
		g_ptr_array_add(table_state, g_object_ref(item));

		/* add report */
		if (fu_hid_report_item_get_kind(item) == FU_HID_ITEM_KIND_MAIN) {
			g_autoptr(GPtrArray) to_remove = g_ptr_array_new();
			g_autoptr(FuHidReport) report = fu_hid_report_new();

			/* copy the table state to the new report */
			for (guint i = 0; i < table_state->len; i++) {
				FuHidReportItem *item_tmp = g_ptr_array_index(table_state, i);
				if (!fu_firmware_add_image_full(FU_FIRMWARE(report),
								FU_FIRMWARE(item_tmp),
								error))
					return FALSE;
			}
			if (!fu_firmware_add_image_full(firmware, FU_FIRMWARE(report), error))
				return FALSE;

			/* remove all the local items */
			for (guint i = 0; i < table_state->len; i++) {
				FuHidReportItem *item_tmp = g_ptr_array_index(table_state, i);
				if (fu_hid_report_item_get_kind(item_tmp) !=
				    FU_HID_ITEM_KIND_GLOBAL)
					g_ptr_array_add(to_remove, item_tmp);
			}
			for (guint i = 0; i < to_remove->len; i++) {
				FuHidReportItem *item_tmp = g_ptr_array_index(to_remove, i);
				g_ptr_array_remove(table_state, item_tmp);
			}
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_hid_descriptor_write_report_item(FuFirmware *report_item,
				    GByteArray *buf,
				    GHashTable *globals,
				    GError **error)
{
	g_autoptr(GBytes) fw = NULL;

	/* dedupe any globals */
	if (fu_hid_report_item_get_kind(FU_HID_REPORT_ITEM(report_item)) ==
	    FU_HID_ITEM_KIND_GLOBAL) {
		guint8 tag = fu_firmware_get_idx(report_item);
		FuFirmware *report_item_tmp = g_hash_table_lookup(globals, GUINT_TO_POINTER(tag));
		if (report_item_tmp != NULL &&
		    fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(report_item)) ==
			fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(report_item_tmp))) {
			g_debug("skipping duplicate global tag 0x%x", tag);
			return TRUE;
		}
		g_hash_table_insert(globals, GUINT_TO_POINTER(tag), report_item);
	}
	fw = fu_firmware_write(report_item, error);
	if (fw == NULL)
		return FALSE;
	fu_byte_array_append_bytes(buf, fw);

	/* success */
	return TRUE;
}

static gboolean
fu_hid_descriptor_write_report(FuFirmware *report,
			       GByteArray *buf,
			       GHashTable *globals,
			       GError **error)
{
	g_autoptr(GPtrArray) report_items = fu_firmware_get_images(report);

	/* for each item */
	for (guint i = 0; i < report_items->len; i++) {
		FuFirmware *report_item = g_ptr_array_index(report_items, i);
		if (!fu_hid_descriptor_write_report_item(report_item, buf, globals, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_hid_descriptor_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GHashTable) globals = g_hash_table_new(g_direct_hash, g_direct_equal);
	g_autoptr(GPtrArray) reports = fu_firmware_get_images(firmware);

	/* for each report */
	for (guint i = 0; i < reports->len; i++) {
		FuFirmware *report = g_ptr_array_index(reports, i);
		if (!fu_hid_descriptor_write_report(report, buf, globals, error))
			return NULL;
	}

	/* success */
	return g_steal_pointer(&buf);
}

/**
 * fu_hid_descriptor_find_report_by_id:
 * @self: a #FuHidDescriptor
 * @usage_page: a HID usage page, or %G_MAXUINT32 for "don't care"
 * @report_id: a HID report id
 * @error: (nullable): optional return location for an error
 *
 * Finds the HID report from the report ID.
 *
 * Returns: (transfer full): A #FuHidReport, or %NULL if not found.
 *
 * Since: 1.9.4
 **/
FuHidReport *
fu_hid_descriptor_find_report_by_id(FuHidDescriptor *self,
				    guint32 usage_page,
				    guint32 report_id,
				    GError **error)
{
	g_autoptr(GPtrArray) reports = fu_firmware_get_images(FU_FIRMWARE(self));

	g_return_val_if_fail(FU_IS_HID_DESCRIPTOR(self), NULL);
	g_return_val_if_fail(report_id != 0x0, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	for (guint i = 0; i < reports->len; i++) {
		FuHidReport *report = g_ptr_array_index(reports, i);
		g_autoptr(FuFirmware) item_id = NULL;

		/* optional, but sometimes required */
		if (usage_page != G_MAXUINT32) {
			g_autoptr(FuFirmware) item_usage_page =
			    fu_firmware_get_image_by_idx(FU_FIRMWARE(report),
							 FU_HID_ITEM_TAG_USAGE_PAGE,
							 NULL);
			if (item_usage_page == NULL)
				continue;
			if (fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_usage_page)) !=
			    usage_page)
				continue;
		}

		/* always required */
		item_id = fu_firmware_get_image_by_idx(FU_FIRMWARE(report),
						       FU_HID_ITEM_TAG_REPORT_ID,
						       NULL);
		if (item_id == NULL)
			continue;
		if (fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_id)) == report_id)
			return g_object_ref(report);
	}
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no report found");
	return NULL;
}

/**
 * fu_hid_descriptor_find_report_by_usage:
 * @self: a #FuHidDescriptor
 * @usage_page: a HID usage page, or %G_MAXUINT32 for "don't care"
 * @usage: a HID usage id
 * @error: (nullable): optional return location for an error
 *
 * Finds the HID report from the report usage.
 *
 * Returns: (transfer full): A #FuHidReport, or %NULL if not found.
 *
 * Since: 1.9.4
 **/
FuHidReport *
fu_hid_descriptor_find_report_by_usage(FuHidDescriptor *self,
				       guint32 usage_page,
				       guint32 usage,
				       GError **error)
{
	g_autoptr(GPtrArray) reports = fu_firmware_get_images(FU_FIRMWARE(self));

	g_return_val_if_fail(FU_IS_HID_DESCRIPTOR(self), NULL);
	g_return_val_if_fail(usage != 0x0, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	for (guint i = 0; i < reports->len; i++) {
		FuHidReport *report = g_ptr_array_index(reports, i);
		g_autoptr(FuFirmware) item_id = NULL;

		/* optional, but sometimes required */
		if (usage_page != G_MAXUINT32) {
			g_autoptr(FuFirmware) item_usage_page =
			    fu_firmware_get_image_by_idx(FU_FIRMWARE(report),
							 FU_HID_ITEM_TAG_USAGE_PAGE,
							 NULL);
			if (item_usage_page == NULL)
				continue;
			if (fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_usage_page)) !=
			    usage_page)
				continue;
		}

		/* always required */
		item_id =
		    fu_firmware_get_image_by_idx(FU_FIRMWARE(report), FU_HID_ITEM_TAG_USAGE, NULL);
		if (item_id == NULL)
			continue;
		if (fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item_id)) == usage)
			return g_object_ref(report);
	}
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no report found");
	return NULL;
}

static void
fu_hid_descriptor_init(FuHidDescriptor *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 1024);
	g_type_ensure(FU_TYPE_HID_REPORT);
	g_type_ensure(FU_TYPE_HID_REPORT_ITEM);
}

static void
fu_hid_descriptor_class_init(FuHidDescriptorClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_hid_descriptor_parse;
	klass_firmware->write = fu_hid_descriptor_write;
}

/**
 * fu_hid_descriptor_new:
 *
 * Creates a new #FuFirmware to parse a HID descriptor
 *
 * Since: 1.9.4
 **/
FuFirmware *
fu_hid_descriptor_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_HID_DESCRIPTOR, NULL));
}
