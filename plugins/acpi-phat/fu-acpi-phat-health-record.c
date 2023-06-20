/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-acpi-phat-health-record.h"
#include "fu-acpi-phat-struct.h"
#include "fu-acpi-phat.h"

struct _FuAcpiPhatHealthRecord {
	FuFirmware parent_instance;
	guint8 am_healthy;
	gchar *guid;
	gchar *device_path;
};

G_DEFINE_TYPE(FuAcpiPhatHealthRecord, fu_acpi_phat_health_record, FU_TYPE_FIRMWARE)

static void
fu_acpi_phat_health_record_export(FuFirmware *firmware,
				  FuFirmwareExportFlags flags,
				  XbBuilderNode *bn)
{
	FuAcpiPhatHealthRecord *self = FU_ACPI_PHAT_HEALTH_RECORD(firmware);
	if (self->guid != NULL)
		fu_xmlb_builder_insert_kv(bn, "guid", self->guid);
	if (self->device_path != NULL)
		fu_xmlb_builder_insert_kv(bn, "device_path", self->device_path);
	if (self->am_healthy != 0)
		fu_xmlb_builder_insert_kx(bn, "am_healthy", self->am_healthy);
}

static gboolean
fu_acpi_phat_health_record_parse(FuFirmware *firmware,
				 GBytes *fw,
				 gsize offset,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuAcpiPhatHealthRecord *self = FU_ACPI_PHAT_HEALTH_RECORD(firmware);
	gsize bufsz = 0;
	guint16 rcdlen;
	guint32 dataoff;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st = NULL;

	/* sanity check record length */
	st = fu_struct_acpi_phat_health_record_parse(buf, bufsz, offset, error);
	if (st == NULL)
		return FALSE;
	rcdlen = fu_struct_acpi_phat_health_record_get_rcdlen(st);
	if (rcdlen != bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "record length not valid: %" G_GUINT16_FORMAT,
			    rcdlen);
		return FALSE;
	}
	self->am_healthy = fu_struct_acpi_phat_health_record_get_flags(st);
	self->guid =
	    fwupd_guid_to_string(fu_struct_acpi_phat_health_record_get_device_signature(st),
				 FWUPD_GUID_FLAG_MIXED_ENDIAN);

	/* device path */
	dataoff = fu_struct_acpi_phat_health_record_get_device_specific_data(st);
	if (bufsz > 28) {
		gsize ubufsz; /* bytes */
		g_autoptr(GBytes) ubuf = NULL;

		/* header -> devicepath -> data */
		if (dataoff == 0x0) {
			ubufsz = bufsz - 28;
		} else {
			ubufsz = dataoff - 28;
		}
		if (ubufsz == 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "device path not valid: %" G_GSIZE_FORMAT,
				    ubufsz);
			return FALSE;
		}

		/* align and convert */
		ubuf = fu_bytes_new_offset(fw, 28, ubufsz, error);
		if (ubuf == NULL)
			return FALSE;
		self->device_path = fu_utf16_to_utf8_bytes(ubuf, error);
		if (self->device_path == NULL)
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_acpi_phat_health_record_write(FuFirmware *firmware, GError **error)
{
	FuAcpiPhatHealthRecord *self = FU_ACPI_PHAT_HEALTH_RECORD(firmware);
	g_autoptr(GByteArray) st = fu_struct_acpi_phat_health_record_new();

	/* convert device path ahead of time */
	if (self->device_path != NULL) {
		g_autoptr(GByteArray) buf =
		    fu_utf8_to_utf16_byte_array(self->device_path, FU_UTF_CONVERT_FLAG_NONE, error);
		if (buf == NULL)
			return NULL;
		g_byte_array_append(st, buf->data, buf->len);
	}

	/* data record */
	if (self->guid != NULL) {
		fwupd_guid_t guid = {0x0};
		if (!fwupd_guid_from_string(self->guid, &guid, FWUPD_GUID_FLAG_MIXED_ENDIAN, error))
			return NULL;
		fu_struct_acpi_phat_health_record_set_device_signature(st, &guid);
	}
	fu_struct_acpi_phat_health_record_set_rcdlen(st, st->len);
	fu_struct_acpi_phat_health_record_set_version(st, fu_firmware_get_version_raw(firmware));
	fu_struct_acpi_phat_health_record_set_flags(st, self->am_healthy);

	/* success */
	return g_steal_pointer(&st);
}

static void
fu_acpi_phat_health_record_set_guid(FuAcpiPhatHealthRecord *self, const gchar *guid)
{
	g_free(self->guid);
	self->guid = g_strdup(guid);
}

static void
fu_acpi_phat_health_record_set_device_path(FuAcpiPhatHealthRecord *self, const gchar *device_path)
{
	g_free(self->device_path);
	self->device_path = g_strdup(device_path);
}

static gboolean
fu_acpi_phat_health_record_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuAcpiPhatHealthRecord *self = FU_ACPI_PHAT_HEALTH_RECORD(firmware);
	const gchar *tmp;
	guint64 tmp64;

	/* optional properties */
	tmp = xb_node_query_text(n, "device_path", NULL);
	if (tmp != NULL)
		fu_acpi_phat_health_record_set_device_path(self, tmp);
	tmp = xb_node_query_text(n, "guid", NULL);
	if (tmp != NULL)
		fu_acpi_phat_health_record_set_guid(self, tmp);
	tmp64 = xb_node_query_text_as_uint(n, "am_healthy", NULL);
	if (tmp64 > G_MAXUINT8) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "am_healthy value invalid, got 0x%x",
			    (guint)tmp64);
		return FALSE;
	}
	self->am_healthy = (guint8)tmp64;

	/* success */
	return TRUE;
}

static void
fu_acpi_phat_health_record_init(FuAcpiPhatHealthRecord *self)
{
}

static void
fu_acpi_phat_health_record_finalize(GObject *object)
{
	FuAcpiPhatHealthRecord *self = FU_ACPI_PHAT_HEALTH_RECORD(object);
	g_free(self->guid);
	g_free(self->device_path);
	G_OBJECT_CLASS(fu_acpi_phat_health_record_parent_class)->finalize(object);
}

static void
fu_acpi_phat_health_record_class_init(FuAcpiPhatHealthRecordClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_acpi_phat_health_record_finalize;
	klass_firmware->parse = fu_acpi_phat_health_record_parse;
	klass_firmware->write = fu_acpi_phat_health_record_write;
	klass_firmware->export = fu_acpi_phat_health_record_export;
	klass_firmware->build = fu_acpi_phat_health_record_build;
}

FuFirmware *
fu_acpi_phat_health_record_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ACPI_PHAT_HEALTH_RECORD, NULL));
}
