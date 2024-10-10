/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuHidDevice"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-hid-descriptor.h"
#include "fu-hid-report-item.h"
#include "fu-hid-struct.h"
#include "fu-input-stream.h"
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

#define FU_HID_DESCRIPTOR_TABLE_LOCAL_SIZE_MAX	 1024
#define FU_HID_DESCRIPTOR_TABLE_LOCAL_DUPES_MAX	 16
#define FU_HID_DESCRIPTOR_TABLE_GLOBAL_SIZE_MAX	 1024
#define FU_HID_DESCRIPTOR_TABLE_GLOBAL_DUPES_MAX 64

static guint
fu_hid_descriptor_count_table_dupes(GPtrArray *table, FuHidReportItem *item)
{
	guint cnt = 0;
	for (guint i = 0; i < table->len; i++) {
		FuHidReportItem *item_tmp = g_ptr_array_index(table, i);
		if (fu_hid_report_item_get_kind(item) == fu_hid_report_item_get_kind(item_tmp) &&
		    fu_hid_report_item_get_value(item) == fu_hid_report_item_get_value(item_tmp) &&
		    fu_firmware_get_idx(FU_FIRMWARE(item)) ==
			fu_firmware_get_idx(FU_FIRMWARE(item_tmp)))
			cnt++;
	}
	return cnt;
}

static gboolean
fu_hid_descriptor_parse(FuFirmware *firmware,
			GInputStream *stream,
			FwupdInstallFlags flags,
			GError **error)
{
	gsize offset = 0;
	gsize streamsz = 0;
	g_autoptr(GPtrArray) table_state =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autoptr(GPtrArray) table_local =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	while (offset < streamsz) {
		g_autofree gchar *itemstr = NULL;
		g_autoptr(FuHidReportItem) item = fu_hid_report_item_new();

		/* sanity check */
		if (table_state->len > FU_HID_DESCRIPTOR_TABLE_GLOBAL_SIZE_MAX) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "HID table state too large, limit is %u",
				    (guint)FU_HID_DESCRIPTOR_TABLE_GLOBAL_SIZE_MAX);
			return FALSE;
		}
		if (table_local->len > FU_HID_DESCRIPTOR_TABLE_LOCAL_SIZE_MAX) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "HID table state too large, limit is %u",
				    (guint)FU_HID_DESCRIPTOR_TABLE_LOCAL_SIZE_MAX);
			return FALSE;
		}

		if (!fu_firmware_parse_stream(FU_FIRMWARE(item), stream, offset, flags, error))
			return FALSE;
		offset += fu_firmware_get_size(FU_FIRMWARE(item));

		/* only for debugging */
		itemstr = fu_firmware_to_string(FU_FIRMWARE(item));
		g_debug("add to table-state:\n%s", itemstr);

		/* if there is a sane number of duplicate tokens then add to table */
		if (fu_hid_report_item_get_kind(item) == FU_HID_ITEM_KIND_GLOBAL) {
			if (fu_hid_descriptor_count_table_dupes(table_state, item) >
			    FU_HID_DESCRIPTOR_TABLE_GLOBAL_DUPES_MAX) {
				g_set_error(
				    error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "table invalid @0x%x, too many duplicate global %s tokens",
				    (guint)offset,
				    fu_firmware_get_id(FU_FIRMWARE(item)));
				return FALSE;
			}
			g_ptr_array_add(table_state, g_object_ref(item));
		} else if (fu_hid_report_item_get_kind(item) == FU_HID_ITEM_KIND_LOCAL ||
			   fu_hid_report_item_get_kind(item) == FU_HID_ITEM_KIND_MAIN) {
			if (fu_hid_descriptor_count_table_dupes(table_local, item) >
			    FU_HID_DESCRIPTOR_TABLE_LOCAL_DUPES_MAX) {
				g_set_error(
				    error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "table invalid @0x%x, too many duplicate %s %s:0x%x tokens",
				    (guint)offset,
				    fu_hid_item_kind_to_string(fu_hid_report_item_get_kind(item)),
				    fu_firmware_get_id(FU_FIRMWARE(item)),
				    fu_hid_report_item_get_value(item));
				return FALSE;
			}
			g_ptr_array_add(table_local, g_object_ref(item));
		}

		/* add report */
		if (fu_hid_report_item_get_kind(item) == FU_HID_ITEM_KIND_MAIN) {
			g_autoptr(FuHidReport) report = fu_hid_report_new();

			/* copy the table state to the new report */
			for (guint i = 0; i < table_state->len; i++) {
				FuHidReportItem *item_tmp = g_ptr_array_index(table_state, i);
				if (!fu_firmware_add_image_full(FU_FIRMWARE(report),
								FU_FIRMWARE(item_tmp),
								error))
					return FALSE;
			}
			for (guint i = 0; i < table_local->len; i++) {
				FuHidReportItem *item_tmp = g_ptr_array_index(table_local, i);
				if (!fu_firmware_add_image_full(FU_FIRMWARE(report),
								FU_FIRMWARE(item_tmp),
								error))
					return FALSE;
			}
			if (!fu_firmware_add_image_full(firmware, FU_FIRMWARE(report), error))
				return FALSE;

			/* remove all the local items */
			g_ptr_array_set_size(table_local, 0);
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

typedef struct {
	const gchar *id;
	guint32 value;
} FuHidDescriptorCondition;

/**
 * fu_hid_descriptor_find_report:
 * @self: a #FuHidDescriptor
 * @error: (nullable): optional return location for an error
 * @...: pairs of string-integer values, ending with %NULL
 *
 * Finds the first HID report that matches all the report attributes.
 *
 * Returns: (transfer full): A #FuHidReport, or %NULL if not found.
 *
 * Since: 1.9.4
 **/
FuHidReport *
fu_hid_descriptor_find_report(FuHidDescriptor *self, GError **error, ...)
{
	va_list args;
	g_autoptr(GPtrArray) conditions = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GPtrArray) reports = fu_firmware_get_images(FU_FIRMWARE(self));

	g_return_val_if_fail(FU_IS_HID_DESCRIPTOR(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* parse varargs */
	va_start(args, error);
	for (guint i = 0; i < 1000; i++) {
		g_autofree FuHidDescriptorCondition *cond = g_new0(FuHidDescriptorCondition, 1);
		cond->id = va_arg(args, const gchar *);
		if (cond->id == NULL)
			break;
		cond->value = va_arg(args, guint32);
		g_ptr_array_add(conditions, g_steal_pointer(&cond));
	}
	va_end(args);

	/* return the first report that matches *all* conditions */
	for (guint i = 0; i < reports->len; i++) {
		FuHidReport *report = g_ptr_array_index(reports, i);
		gboolean matched = TRUE;
		for (guint j = 0; j < conditions->len; j++) {
			FuHidDescriptorCondition *cond = g_ptr_array_index(conditions, j);
			g_autoptr(FuFirmware) item =
			    fu_firmware_get_image_by_id(FU_FIRMWARE(report), cond->id, NULL);
			if (item == NULL) {
				matched = FALSE;
				break;
			}
			if (fu_hid_report_item_get_value(FU_HID_REPORT_ITEM(item)) != cond->value) {
				matched = FALSE;
				break;
			}
		}
		if (matched)
			return g_object_ref(report);
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no report found");
	return NULL;
}

static void
fu_hid_descriptor_init(FuHidDescriptor *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
	fu_firmware_set_size_max(FU_FIRMWARE(self), 64 * 1024);
	fu_firmware_set_images_max(FU_FIRMWARE(self),
				   g_getenv("FWUPD_FUZZER_RUNNING") != NULL ? 10 : 1024);
	g_type_ensure(FU_TYPE_HID_REPORT);
	g_type_ensure(FU_TYPE_HID_REPORT_ITEM);
}

static void
fu_hid_descriptor_class_init(FuHidDescriptorClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_hid_descriptor_parse;
	firmware_class->write = fu_hid_descriptor_write;
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
