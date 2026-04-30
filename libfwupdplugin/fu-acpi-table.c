/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-acpi-table-struct.h"
#include "fu-acpi-table.h"
#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-input-stream.h"
#include "fu-partial-input-stream.h"
#include "fu-sum.h"

/**
 * FuAcpiTable:
 *
 * An generic ACPI table.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	guint8 revision;
	gchar *oem_id;
	gchar *oem_table_id;
	guint32 oem_revision;
	GInputStream *payload;
} FuAcpiTablePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuAcpiTable, fu_acpi_table, FU_TYPE_FIRMWARE)

#define GET_PRIVATE(o) (fu_acpi_table_get_instance_private(o))

static void
fu_acpi_table_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuAcpiTable *self = FU_ACPI_TABLE(firmware);
	FuAcpiTablePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "revision", priv->revision);
	fu_xmlb_builder_insert_kv(bn, "oem_id", priv->oem_id);
	fu_xmlb_builder_insert_kv(bn, "oem_table_id", priv->oem_table_id);
	fu_xmlb_builder_insert_kx(bn, "oem_revision", priv->oem_revision);
}

/**
 * fu_acpi_table_get_revision:
 * @self: a #FuAcpiTable
 *
 * Gets the revision of the table.
 *
 * Returns: integer, default 0x0
 *
 * Since: 1.8.11
 **/
guint8
fu_acpi_table_get_revision(FuAcpiTable *self)
{
	FuAcpiTablePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_ACPI_TABLE(self), G_MAXUINT8);
	return priv->revision;
}

/**
 * fu_acpi_table_get_oem_id:
 * @self: a #FuAcpiTable
 *
 * Gets an optional OEM ID.
 *
 * Returns: a string, or %NULL
 *
 * Since: 1.8.11
 **/
const gchar *
fu_acpi_table_get_oem_id(FuAcpiTable *self)
{
	FuAcpiTablePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_ACPI_TABLE(self), NULL);
	return priv->oem_id;
}

/**
 * fu_acpi_table_get_oem_table_id:
 * @self: a #FuAcpiTable
 *
 * Gets an optional OEM table ID.
 *
 * Returns: a string, or %NULL
 *
 * Since: 1.8.11
 **/
const gchar *
fu_acpi_table_get_oem_table_id(FuAcpiTable *self)
{
	FuAcpiTablePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_ACPI_TABLE(self), NULL);
	return priv->oem_table_id;
}

/**
 * fu_acpi_table_get_oem_revision:
 * @self: a #FuAcpiTable
 *
 * Gets the OEM revision.
 *
 * Returns: integer, default 0x0
 *
 * Since: 1.8.11
 **/
guint32
fu_acpi_table_get_oem_revision(FuAcpiTable *self)
{
	FuAcpiTablePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_ACPI_TABLE(self), G_MAXUINT32);
	return priv->oem_revision;
}

/**
 * fu_acpi_table_get_payload:
 * @self: a #FuAcpiTable
 *
 * Gets the payload after the ACPI header. This function will fail if there is no payload.
 *
 * Returns: (transfer full): a #GInputStream, or %NULL on error
 *
 * Since: 2.1.3
 **/
GInputStream *
fu_acpi_table_get_payload(FuAcpiTable *self, GError **error)
{
	FuAcpiTablePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_ACPI_TABLE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	if (priv->payload == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "no payload");
		return NULL;
	}
	return g_object_ref(priv->payload);
}

static gboolean
fu_acpi_table_parse(FuFirmware *firmware,
		    GInputStream *stream,
		    FuFirmwareParseFlags flags,
		    GError **error)
{
	FuAcpiTable *self = FU_ACPI_TABLE(firmware);
	FuAcpiTablePrivate *priv = GET_PRIVATE(self);
	gsize streamsz = 0;
	guint32 length;
	g_autofree gchar *id = NULL;
	g_autoptr(FuStructAcpiTable) st = NULL;

	/* parse */
	st = fu_struct_acpi_table_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	id = fu_struct_acpi_table_get_signature(st);
	fu_firmware_set_id(FU_FIRMWARE(self), id);
	priv->revision = fu_struct_acpi_table_get_revision(st);
	priv->oem_id = fu_struct_acpi_table_get_oem_id(st);
	priv->oem_table_id = fu_struct_acpi_table_get_oem_table_id(st);
	priv->oem_revision = fu_struct_acpi_table_get_oem_revision(st);

	/* length */
	length = fu_struct_acpi_table_get_length(st);
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (length > streamsz || length < st->buf->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "table length not valid: got 0x%x but expected 0x%x",
			    (guint)streamsz,
			    (guint)length);
		return FALSE;
	}
	fu_firmware_set_size(firmware, length);

	/* checksum */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		guint8 checksum_actual = 0;
		if (!fu_input_stream_compute_sum8(stream, &checksum_actual, error))
			return FALSE;
		if (checksum_actual != 0x0) {
			guint8 checksum = fu_struct_acpi_table_get_checksum(st);
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "CRC failed, expected 0x%02x, got 0x%02x",
				    (guint)checksum - checksum_actual,
				    checksum);
			return FALSE;
		}
	}

	/* optional payload */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM) > 0 &&
	    length != FU_STRUCT_ACPI_TABLE_SIZE) {
		priv->payload = fu_partial_input_stream_new(stream,
							    FU_STRUCT_ACPI_TABLE_SIZE,
							    length - FU_STRUCT_ACPI_TABLE_SIZE,
							    error);
		if (priv->payload == NULL)
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_acpi_table_write(FuFirmware *firmware, GError **error)
{
	FuAcpiTable *self = FU_ACPI_TABLE(firmware);
	FuAcpiTablePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuStructAcpiTable) st = fu_struct_acpi_table_new();
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	g_autoptr(GBytes) payload_blob = NULL;

	/* generate from images if required */
	if (images->len > 0) {
		g_autoptr(GByteArray) payload = g_byte_array_new();
		for (guint i = 0; i < images->len; i++) {
			FuFirmware *img = g_ptr_array_index(images, i);
			g_autoptr(GBytes) fw = fu_firmware_write(img, error);
			if (fw == NULL)
				return NULL;
			fu_byte_array_append_bytes(payload, fw);
		}
		payload_blob = g_byte_array_free_to_bytes(g_steal_pointer(&payload));
	} else if (priv->payload != NULL) {
		payload_blob =
		    fu_input_stream_read_bytes(priv->payload, 0x0, G_MAXSIZE, NULL, error);
		if (payload_blob == NULL)
			return NULL;
	} else {
		payload_blob = g_bytes_new(NULL, 0);
	}

	/* pack */
	if (!fu_struct_acpi_table_set_signature(st, fu_firmware_get_id(firmware), error))
		return NULL;
	fu_struct_acpi_table_set_length(st,
					FU_STRUCT_ACPI_TABLE_SIZE + g_bytes_get_size(payload_blob));
	fu_struct_acpi_table_set_revision(st, priv->revision);
	if (!fu_struct_acpi_table_set_oem_id(st, priv->oem_id, error))
		return NULL;
	if (!fu_struct_acpi_table_set_oem_table_id(st, priv->oem_table_id, error))
		return NULL;
	fu_struct_acpi_table_set_oem_revision(st, priv->oem_revision);
	if (!fu_struct_acpi_table_set_creator_id(st, "FWPD", error))
		return NULL;
	fu_struct_acpi_table_set_creator_revision(st, 0x1);

	/* payload */
	fu_byte_array_append_bytes(st->buf, payload_blob);

	/* fixup checksum */
	fu_struct_acpi_table_set_checksum(st, 0x100 - fu_sum8(st->buf->data, st->buf->len));

	/* success */
	return g_steal_pointer(&st->buf);
}

static void
fu_acpi_table_init(FuAcpiTable *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_set_size_max(FU_FIRMWARE(self), 1 * FU_MB);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 2000);
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_FIRMWARE);
}

static void
fu_acpi_table_finalize(GObject *object)
{
	FuAcpiTable *self = FU_ACPI_TABLE(object);
	FuAcpiTablePrivate *priv = GET_PRIVATE(self);

	if (priv->payload != NULL)
		g_object_unref(priv->payload);
	g_free(priv->oem_table_id);
	g_free(priv->oem_id);
	G_OBJECT_CLASS(fu_acpi_table_parent_class)->finalize(object);
}

static void
fu_acpi_table_class_init(FuAcpiTableClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_acpi_table_finalize;
	firmware_class->parse = fu_acpi_table_parse;
	firmware_class->export = fu_acpi_table_export;
	firmware_class->write = fu_acpi_table_write;
}

/**
 * fu_acpi_table_new:
 *
 * Creates a new #FuFirmware
 *
 * Since: 1.8.11
 **/
FuFirmware *
fu_acpi_table_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ACPI_TABLE, NULL));
}
