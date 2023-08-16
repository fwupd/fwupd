/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-csv-entry.h"
#include "fu-csv-firmware-private.h"
#include "fu-string.h"

/**
 * FuCsvEntry:
 *
 * A comma seporated value entry.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	GPtrArray *values;
} FuCsvEntryPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuCsvEntry, fu_csv_entry, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_csv_entry_get_instance_private(o))

#define FU_CSV_ENTRY_COLUMNS_MAX 1000u

/**
 * fu_csv_entry_add_value:
 * @self: a #FuFirmware
 * @value: (not nullable): string
 *
 * Adds a string value to the entry.
 *
 * Since: 1.9.3
 **/
void
fu_csv_entry_add_value(FuCsvEntry *self, const gchar *value)
{
	FuCsvEntryPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CSV_ENTRY(self));
	g_return_if_fail(value != NULL);
	g_ptr_array_add(priv->values, g_strdup(value));
}

/**
 * fu_csv_entry_get_value_by_idx:
 * @self: a #FuFirmware
 * @idx: column ID idx
 *
 * Gets the entry value for a specific index.
 *
 * Returns: a string, or %NULL if unset
 *
 * Since: 1.9.3
 **/
const gchar *
fu_csv_entry_get_value_by_idx(FuCsvEntry *self, guint idx)
{
	FuCsvEntryPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CSV_ENTRY(self), NULL);
	if (idx >= priv->values->len)
		return NULL;
	return g_ptr_array_index(priv->values, idx);
}

/**
 * fu_csv_entry_get_value_by_column_id:
 * @self: a #FuFirmware
 * @column_id: (not nullable): string, e.g. `component_generation`
 *
 * Gets the entry value for a specific column ID.
 *
 * Returns: a string, or %NULL if unset or the column ID cannot be found
 *
 * Since: 1.9.3
 **/
const gchar *
fu_csv_entry_get_value_by_column_id(FuCsvEntry *self, const gchar *column_id)
{
	FuCsvEntryPrivate *priv = GET_PRIVATE(self);
	FuCsvFirmware *parent = FU_CSV_FIRMWARE(fu_firmware_get_parent(FU_FIRMWARE(self)));
	guint idx = fu_csv_firmware_get_idx_for_column_id(parent, column_id);

	g_return_val_if_fail(FU_IS_CSV_ENTRY(self), NULL);
	g_return_val_if_fail(FU_IS_CSV_FIRMWARE(parent), NULL);
	g_return_val_if_fail(idx != G_MAXUINT, NULL);
	g_return_val_if_fail(column_id != NULL, NULL);

	return g_ptr_array_index(priv->values, idx);
}

gboolean
fu_csv_entry_get_value_by_column_id_uint64(FuCsvEntry *self,
					   const gchar *column_id,
					   guint64 *value,
					   GError **error)
{
	const gchar *str_value = fu_csv_entry_get_value_by_column_id(self, column_id);

	if (str_value == NULL) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "CSV value not found");

		return FALSE;
	}

	return fu_strtoull(str_value, value, 0, G_MAXUINT64, error);
}

static void
fu_ifd_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuCsvEntry *self = FU_CSV_ENTRY(firmware);
	FuCsvEntryPrivate *priv = GET_PRIVATE(self);
	FuCsvFirmware *parent = FU_CSV_FIRMWARE(fu_firmware_get_parent(firmware));
	g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "values", NULL);

	for (guint i = 0; i < priv->values->len; i++) {
		const gchar *value = g_ptr_array_index(priv->values, i);
		const gchar *key = fu_csv_firmware_get_column_id(parent, i);
		if (key != NULL)
			fu_xmlb_builder_insert_kv(bc, key, value);
	}
}

static gboolean
fu_archive_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuCsvEntry *self = FU_CSV_ENTRY(firmware);
	FuCsvFirmware *parent = FU_CSV_FIRMWARE(fu_firmware_get_parent(firmware));
	gboolean add_columns = fu_csv_firmware_get_column_id(parent, 0) == NULL;
	g_autoptr(GPtrArray) values = NULL;

	values = xb_node_query(n, "values/*", 0, error);
	if (values == NULL)
		return FALSE;
	for (guint i = 0; i < values->len; i++) {
		XbNode *c = g_ptr_array_index(values, i);
		if (add_columns && xb_node_get_element(c) != NULL)
			fu_csv_firmware_add_column_id(parent, xb_node_get_element(c));
		fu_csv_entry_add_value(self, xb_node_get_text(c));
	}
	return TRUE;
}

static gboolean
fu_csv_entry_parse_token_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	FuCsvEntry *self = FU_CSV_ENTRY(user_data);
	FuCsvEntryPrivate *priv = GET_PRIVATE(self);
	FuCsvFirmware *parent = FU_CSV_FIRMWARE(fu_firmware_get_parent(FU_FIRMWARE(self)));
	const gchar *column_id = fu_csv_firmware_get_column_id(parent, token_idx);

	/* sanity check */
	if (token_idx > FU_CSV_ENTRY_COLUMNS_MAX) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "too many columns, limit is %u",
			    FU_CSV_ENTRY_COLUMNS_MAX);
		return FALSE;
	}

	if (g_strcmp0(column_id, "$id") == 0) {
		g_ptr_array_add(priv->values, NULL);
		fu_firmware_set_id(FU_FIRMWARE(self), token->str);
		return TRUE;
	}
	if (g_strcmp0(column_id, "$idx") == 0) {
		guint64 value = 0;
		if (!fu_strtoull(token->str, &value, 0, G_MAXUINT64, error))
			return FALSE;
		g_ptr_array_add(priv->values, NULL);
		fu_firmware_set_idx(FU_FIRMWARE(self), value);
		return TRUE;
	}
	if (g_strcmp0(column_id, "$version") == 0) {
		g_ptr_array_add(priv->values, NULL);
		fu_firmware_set_version(FU_FIRMWARE(self), token->str);
		return TRUE;
	}
	if (g_strcmp0(column_id, "$version_raw") == 0) {
		guint64 value = 0;
		if (!fu_strtoull(token->str, &value, 0, G_MAXUINT64, error))
			return FALSE;
		g_ptr_array_add(priv->values, NULL);
		fu_firmware_set_version_raw(FU_FIRMWARE(self), value);
		return TRUE;
	}

	g_ptr_array_add(priv->values, g_strdup(token->str));
	return TRUE;
}

static gboolean
fu_csv_entry_parse(FuFirmware *firmware,
		   GBytes *fw,
		   gsize offset,
		   FwupdInstallFlags flags,
		   GError **error)
{
	FuCsvEntry *self = FU_CSV_ENTRY(firmware);
	return fu_strsplit_full((const gchar *)g_bytes_get_data(fw, NULL),
				g_bytes_get_size(fw),
				",",
				fu_csv_entry_parse_token_cb,
				self,
				error);
}

static GByteArray *
fu_csv_entry_write(FuFirmware *firmware, GError **error)
{
	FuCsvEntry *self = FU_CSV_ENTRY(firmware);
	FuCsvEntryPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GString) str = g_string_new(NULL);

	/* single line */
	for (guint i = 0; i < priv->values->len; i++) {
		const gchar *value = g_ptr_array_index(priv->values, i);
		if (str->len > 0)
			g_string_append(str, ",");
		g_string_append(str, value);
	}
	g_string_append(str, "\n");
	g_byte_array_append(buf, (const guint8 *)str->str, str->len);

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_csv_entry_init(FuCsvEntry *self)
{
	FuCsvEntryPrivate *priv = GET_PRIVATE(self);
	priv->values = g_ptr_array_new_with_free_func(g_free);
}

static void
fu_csv_entry_finalize(GObject *object)
{
	FuCsvEntry *self = FU_CSV_ENTRY(object);
	FuCsvEntryPrivate *priv = GET_PRIVATE(self);
	g_ptr_array_unref(priv->values);
	G_OBJECT_CLASS(fu_csv_entry_parent_class)->finalize(object);
}

static void
fu_csv_entry_class_init(FuCsvEntryClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_csv_entry_finalize;
	klass_firmware->parse = fu_csv_entry_parse;
	klass_firmware->write = fu_csv_entry_write;
	klass_firmware->build = fu_archive_firmware_build;
	klass_firmware->export = fu_ifd_firmware_export;
}

/**
 * fu_csv_entry_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 1.9.3
 **/
FuFirmware *
fu_csv_entry_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_CSV_ENTRY, NULL));
}
