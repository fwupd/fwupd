/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-acpi-table-struct.h"
#include "fu-acpi-table.h"
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

static gboolean
fu_acpi_table_parse(FuFirmware *firmware,
		    GBytes *fw,
		    gsize offset,
		    FwupdInstallFlags flags,
		    GError **error)
{
	FuAcpiTable *self = FU_ACPI_TABLE(firmware);
	FuAcpiTablePrivate *priv = GET_PRIVATE(self);
	gsize bufsz = 0;
	guint32 length;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *id = NULL;
	g_autoptr(GByteArray) st = NULL;

	/* parse */
	st = fu_struct_acpi_table_parse(buf, bufsz, offset, error);
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
	if (length > bufsz || length < st->len) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "table length not valid: got 0x%x but expected 0x%x",
			    (guint)bufsz,
			    (guint)length);
		return FALSE;
	}
	fu_firmware_set_size(firmware, length);

	/* checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint8 checksum_actual = fu_sum8(buf, length);
		if (checksum_actual != 0x0) {
			guint8 checksum = fu_struct_acpi_table_get_checksum(st);
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "CRC failed, expected %02x, got %02x",
				    (guint)checksum - checksum_actual,
				    checksum);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_acpi_table_init(FuAcpiTable *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_acpi_table_finalize(GObject *object)
{
	FuAcpiTable *self = FU_ACPI_TABLE(object);
	FuAcpiTablePrivate *priv = GET_PRIVATE(self);
	g_free(priv->oem_table_id);
	g_free(priv->oem_id);
	G_OBJECT_CLASS(fu_acpi_table_parent_class)->finalize(object);
}

static void
fu_acpi_table_class_init(FuAcpiTableClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_acpi_table_finalize;
	klass_firmware->parse = fu_acpi_table_parse;
	klass_firmware->export = fu_acpi_table_export;
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
