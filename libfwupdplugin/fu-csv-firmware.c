/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-byte-array.h"
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
 * NOTE: As a special magic feature, a @column_id of `$id` also sets the firmware ID.
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
	fu_firmware_add_image(FU_FIRMWARE(self), entry);
	if (!fu_firmware_parse(entry, fw, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_csv_firmware_parse(FuFirmware *firmware,
		      GBytes *fw,
		      gsize offset,
		      FwupdInstallFlags flags,
		      GError **error)
{
	FuCsvFirmware *self = FU_CSV_FIRMWARE(firmware);
	return fu_strsplit_full((const gchar *)g_bytes_get_data(fw, NULL),
				g_bytes_get_size(fw),
				"\n",
				fu_csv_firmware_parse_line_cb,
				self,
				error);
}

static GByteArray *
fu_csv_firmware_write(FuFirmware *firmware, GError **error)
{
	FuCsvFirmware *self = FU_CSV_FIRMWARE(firmware);
	FuCsvFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);
	g_autoptr(GString) str = g_string_new("#");

	/* title section */
	for (guint i = 0; i < priv->column_ids->len; i++) {
		const gchar *column_id = g_ptr_array_index(priv->column_ids, i);
		if (str->len > 1)
			g_string_append(str, ",");
		g_string_append(str, column_id);
	}
	g_string_append(str, "\n");
	g_byte_array_append(buf, (const guint8 *)str->str, str->len);

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
fu_csv_firmware_init(FuCsvFirmware *self)
{
	FuCsvFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->column_ids = g_ptr_array_new_with_free_func(g_free);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_csv_firmware_finalize;
	klass_firmware->parse = fu_csv_firmware_parse;
	klass_firmware->write = fu_csv_firmware_write;
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
