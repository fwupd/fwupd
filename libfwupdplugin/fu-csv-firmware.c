/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-csv-entry.h"
#include "fu-csv-firmware-private.h"
#include "fu-string.h"

/**
 * FuCsvFirmware:
 *
 * A comma seporated value file.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	GPtrArray *column_ids;
	gboolean write_column_ids;
} FuCsvFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuCsvFirmware, fu_csv_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_csv_firmware_get_instance_private(o))

/**
 * fu_csv_firmware_add_column_id:
 * @self: a #FuFirmware
 * @column_id: (not nullable): string, e.g. `component_generation`
 *
 * Adds a column ID.
 *
 * There are several optional magic column IDs that map to #FuFirmware properties:
 *
 * * `$id` sets the firmware ID
 * * `$idx` sets the firmware index
 * * `$version` sets the firmware version
 * * `$version_raw` sets the raw firmware version
 *
 * Since: 1.9.3
 **/
void
fu_csv_firmware_add_column_id(FuCsvFirmware *self, const gchar *column_id)
{
	FuCsvFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CSV_FIRMWARE(self));
	g_return_if_fail(column_id != NULL);
	g_ptr_array_add(priv->column_ids, g_strdup(column_id));
}

/**
 * fu_csv_firmware_get_column_id:
 * @self: a #FuFirmware
 * @idx: column ID idx
 *
 * Gets the column ID for a specific index position.
 *
 * Returns: a string, or %NULL if not found
 *
 * Since: 1.9.3
 **/
const gchar *
fu_csv_firmware_get_column_id(FuCsvFirmware *self, guint idx)
{
	FuCsvFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CSV_FIRMWARE(self), NULL);

	if (idx >= priv->column_ids->len)
		return NULL;
	return g_ptr_array_index(priv->column_ids, idx);
}

/**
 * fu_csv_firmware_get_idx_for_column_id:
 * @self: a #FuFirmware
 * @column_id: (not nullable): string, e.g. `component_generation`
 *
 * Gets the column index for a given column ID.
 *
 * Returns: position, or %G_MAXUINT if unset
 *
 * Since: 1.9.3
 **/
guint
fu_csv_firmware_get_idx_for_column_id(FuCsvFirmware *self, const gchar *column_id)
{
	FuCsvFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CSV_FIRMWARE(self), G_MAXUINT);
	g_return_val_if_fail(column_id != NULL, G_MAXUINT);
	for (guint i = 0; i < priv->column_ids->len; i++) {
		const gchar *column_id_tmp = g_ptr_array_index(priv->column_ids, i);
		if (g_strcmp0(column_id_tmp, column_id) == 0)
			return i;
	}
	return G_MAXUINT;
}

/**
 * fu_csv_firmware_set_write_column_ids:
 * @self: a #FuFirmware
 * @write_column_ids: boolean
 *
 * Sets if we should write the column ID headers on export.
 *
 * Since: 2.0.0
 **/
void
fu_csv_firmware_set_write_column_ids(FuCsvFirmware *self, gboolean write_column_ids)
{
	FuCsvFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CSV_FIRMWARE(self));
	priv->write_column_ids = write_column_ids;
}

/**
 * fu_csv_firmware_get_write_column_ids:
 * @self: a #FuFirmware
 *
 * Gets if we should write the column ID headers on export.
 *
 * Returns: boolean.
 *
 * Since: 2.0.0
 **/
gboolean
fu_csv_firmware_get_write_column_ids(FuCsvFirmware *self)
{
	FuCsvFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CSV_FIRMWARE(self), FALSE);
	return priv->write_column_ids;
}

static gboolean
fu_csv_firmware_parse_token_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	FuCsvFirmware *self = FU_CSV_FIRMWARE(user_data);
	fu_csv_firmware_add_column_id(self, token->str);
	return TRUE;
}

static gboolean
fu_csv_firmware_parse_line_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	FuCsvFirmware *self = FU_CSV_FIRMWARE(user_data);
	g_autoptr(FuFirmware) entry = fu_csv_entry_new();
	g_autoptr(GBytes) fw = NULL;

	/* ignore blank lines */
	if (token->len == 0)
		return TRUE;

	/* title */
	if (g_str_has_prefix(token->str, "#")) {
		return fu_strsplit_full(token->str + 1,
					token->len - 1,
					",",
					fu_csv_firmware_parse_token_cb,
					self,
					error);
		return TRUE;
	}

	/* parse entry */
	fw = g_bytes_new(token->str, token->len);
	fu_firmware_set_idx(entry, token_idx);
	if (!fu_firmware_add_image_full(FU_FIRMWARE(self), entry, error))
		return FALSE;
	if (!fu_firmware_parse_bytes(entry, fw, 0x0, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_csv_firmware_parse(FuFirmware *firmware,
		      GInputStream *stream,
		      FwupdInstallFlags flags,
		      GError **error)
{
	FuCsvFirmware *self = FU_CSV_FIRMWARE(firmware);
	return fu_strsplit_stream(stream, 0x0, "\n", fu_csv_firmware_parse_line_cb, self, error);
}

static GByteArray *
fu_csv_firmware_write(FuFirmware *firmware, GError **error)
{
	FuCsvFirmware *self = FU_CSV_FIRMWARE(firmware);
	FuCsvFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* title section */
	if (priv->write_column_ids) {
		g_autoptr(GString) str = g_string_new("#");
		for (guint i = 0; i < priv->column_ids->len; i++) {
			const gchar *column_id = g_ptr_array_index(priv->column_ids, i);
			if (str->len > 1)
				g_string_append(str, ",");
			g_string_append(str, column_id);
		}
		g_string_append(str, "\n");
		g_byte_array_append(buf, (const guint8 *)str->str, str->len);
	}

	/* each entry */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) img_blob = fu_firmware_write(img, error);
		if (img_blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf, img_blob);
	}

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_csv_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuCsvFirmware *self = FU_CSV_FIRMWARE(firmware);
	FuCsvFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kb(bn, "write_column_ids", priv->write_column_ids);
}

static gboolean
fu_csv_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuCsvFirmware *self = FU_CSV_FIRMWARE(firmware);
	FuCsvFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;

	/* optional properties */
	tmp = xb_node_query_text(n, "write_column_ids", NULL);
	if (tmp != NULL) {
		if (!fu_strtobool(tmp, &priv->write_column_ids, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_csv_firmware_init(FuCsvFirmware *self)
{
	FuCsvFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->column_ids = g_ptr_array_new_with_free_func(g_free);
	priv->write_column_ids = TRUE;
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 10000);
	g_type_ensure(FU_TYPE_CSV_ENTRY);
}

static void
fu_csv_firmware_finalize(GObject *object)
{
	FuCsvFirmware *self = FU_CSV_FIRMWARE(object);
	FuCsvFirmwarePrivate *priv = GET_PRIVATE(self);
	g_ptr_array_unref(priv->column_ids);
	G_OBJECT_CLASS(fu_csv_firmware_parent_class)->finalize(object);
}

static void
fu_csv_firmware_class_init(FuCsvFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_csv_firmware_finalize;
	firmware_class->parse = fu_csv_firmware_parse;
	firmware_class->write = fu_csv_firmware_write;
	firmware_class->export = fu_csv_firmware_export;
	firmware_class->build = fu_csv_firmware_build;
}

/**
 * fu_csv_firmware_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 1.9.3
 **/
FuFirmware *
fu_csv_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_CSV_FIRMWARE, NULL));
}
