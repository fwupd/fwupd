/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-acpi-table.h"
#include "fu-mem.h"
#include "fu-string.h"
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

/* almost all ACPI tables have this structure */
typedef struct __attribute__((packed)) {
	gchar signature[4];
	guint32 length;
	guint8 revision;
	guint8 checksum;
	gchar oem_id[6];
	gchar oem_table_id[8];
	guint32 oem_revision;
	gchar asl_compiler_id[4];
	guint32 asl_compiler_revision;
} FuAcpiTableHdr;

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
	gchar oem_id[6] = {'\0'};
	gchar oem_table_id[8] = {'\0'};
	gchar signature[4] = {'\0'};
	gsize bufsz = 0;
	guint32 length = 0;
	guint8 checksum = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *id = NULL;

	/* signature */
	if (!fu_memcpy_safe((guint8 *)signature,
			    sizeof(signature),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset + G_STRUCT_OFFSET(FuAcpiTableHdr, signature), /* src */
			    sizeof(signature),
			    error))
		return FALSE;
	id = fu_strsafe(signature, sizeof(signature));
	fu_firmware_set_id(FU_FIRMWARE(self), id);

	/* length */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuAcpiTableHdr, length),
				    &length,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (length > bufsz || length < sizeof(FuAcpiTableHdr)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "table length not valid: got 0x%x but expected 0x%x",
			    (guint)bufsz,
			    (guint)length);
		return FALSE;
	}
	fu_firmware_set_size(firmware, length);

	/* revision */
	if (!fu_memread_uint8_safe(buf,
				   bufsz,
				   offset + G_STRUCT_OFFSET(FuAcpiTableHdr, revision),
				   &priv->revision,
				   error))
		return FALSE;

	/* checksum */
	if (!fu_memread_uint8_safe(buf,
				   bufsz,
				   offset + G_STRUCT_OFFSET(FuAcpiTableHdr, checksum),
				   &checksum,
				   error))
		return FALSE;
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint8 checksum_actual = fu_sum8(buf, length);
		if (checksum_actual != 0x0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "CRC failed, expected %02x, got %02x",
				    (guint)checksum - checksum_actual,
				    checksum);
			return FALSE;
		}
	}

	/* OEM ID */
	if (!fu_memcpy_safe((guint8 *)oem_id,
			    sizeof(oem_id),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset + G_STRUCT_OFFSET(FuAcpiTableHdr, oem_id), /* src */
			    sizeof(oem_id),
			    error))
		return FALSE;
	priv->oem_id = fu_strsafe(oem_id, sizeof(oem_id));

	/* OEM table ID */
	if (!fu_memcpy_safe((guint8 *)oem_table_id,
			    sizeof(oem_table_id),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset + G_STRUCT_OFFSET(FuAcpiTableHdr, oem_table_id), /* src */
			    sizeof(oem_table_id),
			    error))
		return FALSE;
	priv->oem_table_id = fu_strsafe(oem_table_id, sizeof(oem_table_id));

	/* OEM revision */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuAcpiTableHdr, oem_revision),
				    &priv->oem_revision,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

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
