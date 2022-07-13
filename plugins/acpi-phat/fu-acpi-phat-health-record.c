/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-acpi-phat-health-record.h"
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
	guint16 rcdlen = 0;
	guint32 dataoff = 0x0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	fwupd_guid_t guid = {0x0};

	/* record length */
	if (!fu_memread_uint16_safe(buf, bufsz, 2, &rcdlen, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (rcdlen != bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "record length not valid: %" G_GUINT16_FORMAT,
			    rcdlen);
		return FALSE;
	}

	/* am healthy */
	if (!fu_memread_uint8_safe(buf, bufsz, 7, &self->am_healthy, error))
		return FALSE;

	/* device signature */
	if (!fu_memcpy_safe((guint8 *)&guid,
			    sizeof(guid),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    8, /* src */
			    sizeof(guid),
			    error))
		return FALSE;
	self->guid = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);

	/* read the data offset to work out the size of the middle part */
	if (!fu_memread_uint32_safe(buf, bufsz, 24, &dataoff, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* device path */
	if (bufsz > 28) {
		gsize ubufsz; /* bytes */
		g_autofree gunichar2 *ubuf = NULL;

		/* header -> devicepath -> data */
		if (dataoff == 0x0) {
			ubufsz = bufsz - 28;
		} else {
			ubufsz = dataoff - 28;
		}
		if (ubufsz > bufsz) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "device path too large: 0x%x",
				    (guint)ubufsz);
			return FALSE;
		}

		/* check this is an even number of bytes */
		if (ubufsz == 0 || ubufsz % 2 != 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "device path not valid: %" G_GSIZE_FORMAT,
				    ubufsz);
			return FALSE;
		}

		/* align and convert */
		ubuf = g_new0(gunichar2, ubufsz / 2);
		if (!fu_memcpy_safe((guint8 *)ubuf,
				    ubufsz,
				    0x0, /* dst */
				    buf,
				    bufsz,
				    28, /* src */
				    ubufsz,
				    error))
			return FALSE;
		self->device_path = g_utf16_to_utf8(ubuf, ubufsz / 2, NULL, NULL, error);
		if (self->device_path == NULL)
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_acpi_phat_health_record_write(FuFirmware *firmware, GError **error)
{
	FuAcpiPhatHealthRecord *self = FU_ACPI_PHAT_HEALTH_RECORD(firmware);
	fwupd_guid_t guid = {0x0};
	glong device_path_utf16sz = 0;
	g_autofree gunichar2 *device_path_utf16 = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* convert device path ahead of time to get total record length */
	if (self->device_path != NULL) {
		device_path_utf16 =
		    g_utf8_to_utf16(self->device_path, -1, NULL, &device_path_utf16sz, error);
		if (device_path_utf16 == NULL)
			return NULL;
		device_path_utf16sz *= 2;
	}

	/* data record */
	fu_byte_array_append_uint16(buf, FU_ACPI_PHAT_RECORD_TYPE_HEALTH, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, 28 + device_path_utf16sz, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint8(buf, fu_firmware_get_version_raw(firmware));
	fu_byte_array_append_uint8(buf, 0x00);
	fu_byte_array_append_uint8(buf, 0x00);
	fu_byte_array_append_uint8(buf, self->am_healthy);

	/* device signature */
	if (self->guid != NULL) {
		if (!fwupd_guid_from_string(self->guid, &guid, FWUPD_GUID_FLAG_MIXED_ENDIAN, error))
			return NULL;
	}
	g_byte_array_append(buf, guid, sizeof(guid));

	/* device-specific data unsupported */
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN);

	/* device path */
	if (self->device_path != NULL) {
		g_byte_array_append(buf, (const guint8 *)device_path_utf16, device_path_utf16sz);
	}

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
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
