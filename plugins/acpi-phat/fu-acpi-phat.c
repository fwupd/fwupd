/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-acpi-phat-health-record.h"
#include "fu-acpi-phat-version-record.h"
#include "fu-acpi-phat.h"

struct _FuAcpiPhat {
	FuFirmware parent_instance;
	gchar *oem_id;
};

G_DEFINE_TYPE(FuAcpiPhat, fu_acpi_phat, FU_TYPE_FIRMWARE)

static void
fu_acpi_phat_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuAcpiPhat *self = FU_ACPI_PHAT(firmware);
	if (self->oem_id != NULL)
		fu_xmlb_builder_insert_kv(bn, "oem_id", self->oem_id);
}

static gboolean
fu_acpi_phat_record_parse(FuFirmware *firmware,
			  GBytes *fw,
			  gsize *offset,
			  FwupdInstallFlags flags,
			  GError **error)
{
	gsize bufsz = 0;
	guint16 record_length = 0;
	guint16 record_type = 0;
	guint8 revision;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(FuFirmware) firmware_rcd = NULL;

	/* common header */
	if (!fu_memread_uint16_safe(buf, bufsz, *offset, &record_type, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    *offset + 2,
				    &record_length,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (record_length < 5) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "PHAT record length invalid, got 0x%x",
			    record_length);
		return FALSE;
	}
	if (!fu_memread_uint8_safe(buf, bufsz, *offset + 4, &revision, error))
		return FALSE;

	/* firmware version data record */
	if (record_type == FU_ACPI_PHAT_RECORD_TYPE_VERSION) {
		firmware_rcd = fu_acpi_phat_version_record_new();
	} else if (record_type == FU_ACPI_PHAT_RECORD_TYPE_HEALTH) {
		firmware_rcd = fu_acpi_phat_health_record_new();
	}

	/* supported record type */
	if (firmware_rcd != NULL) {
		g_autoptr(GBytes) fw_tmp = NULL;
		fw_tmp = fu_bytes_new_offset(fw, *offset, record_length, error);
		if (fw_tmp == NULL)
			return FALSE;
		fu_firmware_set_size(firmware_rcd, record_length);
		fu_firmware_set_offset(firmware_rcd, *offset);
		fu_firmware_set_version_raw(firmware_rcd, revision);
		if (!fu_firmware_parse(firmware_rcd, fw_tmp, flags, error))
			return FALSE;
		fu_firmware_add_image(firmware, firmware_rcd);
	}

	*offset += record_length;
	return TRUE;
}

static void
fu_acpi_phat_set_oem_id(FuAcpiPhat *self, const gchar *oem_id)
{
	g_free(self->oem_id);
	self->oem_id = g_strdup(oem_id);
}

static gboolean
fu_acpi_phat_parse(FuFirmware *firmware,
		   GBytes *fw,
		   gsize offset,
		   FwupdInstallFlags flags,
		   GError **error)
{
	FuAcpiPhat *self = FU_ACPI_PHAT(firmware);
	gchar oem_id[6] = {'\0'};
	gchar oem_table_id[8] = {'\0'};
	gchar signature[4] = {'\0'};
	gsize bufsz = 0;
	guint32 length = 0;
	guint32 oem_revision = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *oem_id_safe = NULL;
	g_autofree gchar *oem_table_id_safe = NULL;

	/* parse table */
	if (!fu_memcpy_safe((guint8 *)signature,
			    sizeof(signature),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset + 0, /* src */
			    sizeof(signature),
			    error))
		return FALSE;
	if (memcmp(signature, "PHAT", 4) != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "Not a PHAT table, got %s",
			    signature);
		return FALSE;
	}
	if (!fu_memread_uint32_safe(buf, bufsz, offset + 4, &length, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (bufsz < length) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "PHAT table invalid size, got 0x%x, expected 0x%x",
			    (guint)bufsz,
			    length);
		return FALSE;
	}

	/* spec revision */
	if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		guint8 revision = 0;
		if (!fu_memread_uint8_safe(buf, bufsz, offset + 8, &revision, error))
			return FALSE;
		if (revision != FU_ACPI_PHAT_REVISION) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "PHAT table revision invalid, got 0x%x, expected 0x%x",
				    revision,
				    (guint)FU_ACPI_PHAT_REVISION);
			return FALSE;
		}
	}

	/* verify checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint8 checksum = fu_sum8(buf, length);
		if (checksum != 0x00) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "PHAT table checksum invalid, got 0x%x",
				    checksum);
			return FALSE;
		}
	}

	/* OEMID */
	if (!fu_memcpy_safe((guint8 *)oem_id,
			    sizeof(oem_id),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset + 10, /* src */
			    sizeof(oem_id),
			    error))
		return FALSE;
	oem_id_safe = fu_strsafe((const gchar *)oem_id, sizeof(oem_id));
	fu_acpi_phat_set_oem_id(self, oem_id_safe);

	/* OEM Table ID */
	if (!fu_memcpy_safe((guint8 *)oem_table_id,
			    sizeof(oem_table_id),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset + 16, /* src */
			    sizeof(oem_table_id),
			    error))
		return FALSE;
	oem_table_id_safe = fu_strsafe((const gchar *)oem_table_id, sizeof(oem_table_id));
	fu_firmware_set_id(firmware, oem_table_id_safe);
	if (!fu_memread_uint32_safe(buf, bufsz, offset + 24, &oem_revision, G_LITTLE_ENDIAN, error))
		return FALSE;
	fu_firmware_set_version_raw(firmware, oem_revision);

	/* platform telemetry records */
	for (gsize offset_tmp = offset + 36; offset_tmp < length;) {
		if (!fu_acpi_phat_record_parse(firmware, fw, &offset_tmp, flags, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_acpi_phat_write(FuFirmware *firmware, GError **error)
{
	FuAcpiPhat *self = FU_ACPI_PHAT(firmware);
	const gchar *oem_table_id_str = fu_firmware_get_id(firmware);
	guint8 creator_id[] = {'F', 'W', 'U', 'P'};
	guint8 creator_rev[] = {'0', '0', '0', '0'};
	guint8 oem_id[6] = {'\0'};
	guint8 oem_table_id[8] = {'\0'};
	guint8 signature[] = {'P', 'H', 'A', 'T'};
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) buf2 = g_byte_array_new();
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);

	/* write each image so we get the total size */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) blob = fu_firmware_write(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf2, blob);
	}

	/* header */
	g_byte_array_append(buf, signature, sizeof(signature));
	fu_byte_array_append_uint32(buf, buf2->len + 36, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8(buf, fu_firmware_get_version_raw(firmware));
	fu_byte_array_append_uint8(buf, 0xFF); /* will fixup */
	if (self->oem_id != NULL) {
		gsize oem_id_strlen = strlen(self->oem_id);
		if (!fu_memcpy_safe(oem_id,
				    sizeof(oem_id),
				    0x0, /* dst */
				    (const guint8 *)self->oem_id,
				    oem_id_strlen,
				    0x0, /* src */
				    oem_id_strlen,
				    error))
			return NULL;
	}
	g_byte_array_append(buf, oem_id, sizeof(oem_id));
	if (oem_table_id_str != NULL) {
		gsize oem_table_id_strlen = strlen(oem_table_id_str);
		if (!fu_memcpy_safe(oem_table_id,
				    sizeof(oem_table_id),
				    0x0, /* dst */
				    (const guint8 *)oem_table_id_str,
				    oem_table_id_strlen,
				    0x0, /* src */
				    oem_table_id_strlen,
				    error))
			return NULL;
	}
	g_byte_array_append(buf, oem_table_id, sizeof(oem_table_id));
	fu_byte_array_append_uint32(buf, fu_firmware_get_version_raw(firmware), G_LITTLE_ENDIAN);
	g_byte_array_append(buf, creator_id, sizeof(creator_id));
	g_byte_array_append(buf, creator_rev, sizeof(creator_rev));
	g_byte_array_append(buf, buf2->data, buf2->len);

	/* fixup checksum */
	buf->data[9] = 0xFF - fu_sum8(buf->data, buf->len);

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static gboolean
fu_acpi_phat_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuAcpiPhat *self = FU_ACPI_PHAT(firmware);
	const gchar *tmp;

	/* optional properties */
	tmp = xb_node_query_text(n, "oem_id", NULL);
	if (tmp != NULL)
		fu_acpi_phat_set_oem_id(self, tmp);

	/* success */
	return TRUE;
}

static gboolean
fu_acpi_phat_to_report_string_cb(XbBuilderNode *bn, gpointer user_data)
{
	if (g_strcmp0(xb_builder_node_get_element(bn), "offset") == 0 ||
	    g_strcmp0(xb_builder_node_get_element(bn), "flags") == 0 ||
	    g_strcmp0(xb_builder_node_get_element(bn), "size") == 0)
		xb_builder_node_add_flag(bn, XB_BUILDER_NODE_FLAG_IGNORE);
	return FALSE;
}

gchar *
fu_acpi_phat_to_report_string(FuAcpiPhat *self)
{
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("firmware");
	fu_firmware_export(FU_FIRMWARE(self), FU_FIRMWARE_EXPORT_FLAG_NONE, bn);
	xb_builder_node_traverse(bn,
				 G_PRE_ORDER,
				 G_TRAVERSE_ALL,
				 3,
				 fu_acpi_phat_to_report_string_cb,
				 NULL);
	return xb_builder_node_export(bn,
				      XB_NODE_EXPORT_FLAG_FORMAT_MULTILINE |
					  XB_NODE_EXPORT_FLAG_FORMAT_INDENT,
				      NULL);
}

static void
fu_acpi_phat_init(FuAcpiPhat *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_acpi_phat_finalize(GObject *object)
{
	FuAcpiPhat *self = FU_ACPI_PHAT(object);
	g_free(self->oem_id);
	G_OBJECT_CLASS(fu_acpi_phat_parent_class)->finalize(object);
}

static void
fu_acpi_phat_class_init(FuAcpiPhatClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_acpi_phat_finalize;
	klass_firmware->parse = fu_acpi_phat_parse;
	klass_firmware->write = fu_acpi_phat_write;
	klass_firmware->export = fu_acpi_phat_export;
	klass_firmware->build = fu_acpi_phat_build;
}

FuFirmware *
fu_acpi_phat_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ACPI_PHAT, NULL));
}
