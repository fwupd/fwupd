/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-acpi-phat-version-element.h"

struct _FuAcpiPhatVersionElement {
	FuFirmware parent_instance;
	gchar *guid;
	gchar *producer_id;
};

G_DEFINE_TYPE(FuAcpiPhatVersionElement, fu_acpi_phat_version_element, FU_TYPE_FIRMWARE)

static void
fu_acpi_phat_version_element_export(FuFirmware *firmware,
				    FuFirmwareExportFlags flags,
				    XbBuilderNode *bn)
{
	FuAcpiPhatVersionElement *self = FU_ACPI_PHAT_VERSION_ELEMENT(firmware);
	if (self->guid != NULL)
		fu_xmlb_builder_insert_kv(bn, "guid", self->guid);
	if (self->producer_id != NULL)
		fu_xmlb_builder_insert_kv(bn, "producer_id", self->producer_id);
}

static gboolean
fu_acpi_phat_version_element_parse(FuFirmware *firmware,
				   GBytes *fw,
				   gsize offset,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuAcpiPhatVersionElement *self = FU_ACPI_PHAT_VERSION_ELEMENT(firmware);
	fwupd_guid_t component_id = {0x0};
	gchar producer_id[4] = {'\0'};
	gsize bufsz = 0;
	guint64 version_value = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* hardcoded */
	fu_firmware_set_size(firmware, 28);

	if (!fu_memcpy_safe((guint8 *)&component_id,
			    sizeof(component_id),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    0, /* src */
			    sizeof(component_id),
			    error))
		return FALSE;
	self->guid = fwupd_guid_to_string(&component_id, FWUPD_GUID_FLAG_MIXED_ENDIAN);

	if (!fu_memread_uint64_safe(buf, bufsz, 16, &version_value, G_LITTLE_ENDIAN, error))
		return FALSE;
	fu_firmware_set_version_raw(firmware, version_value);
	if (!fu_memcpy_safe((guint8 *)producer_id,
			    sizeof(producer_id),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    24, /* src */
			    sizeof(producer_id),
			    error))
		return FALSE;
	if (memcmp(producer_id, "\0\0\0\0", 4) == 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "PHAT version element invalid");
		return FALSE;
	}
	self->producer_id = fu_strsafe((const gchar *)producer_id, sizeof(producer_id));
	return TRUE;
}

static GBytes *
fu_acpi_phat_version_element_write(FuFirmware *firmware, GError **error)
{
	FuAcpiPhatVersionElement *self = FU_ACPI_PHAT_VERSION_ELEMENT(firmware);
	fwupd_guid_t guid = {0x0};
	guint8 producer_id[4] = {'\0'};
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* component ID */
	if (self->guid != NULL) {
		if (!fwupd_guid_from_string(self->guid, &guid, FWUPD_GUID_FLAG_MIXED_ENDIAN, error))
			return NULL;
	}
	g_byte_array_append(buf, guid, sizeof(guid));

	/* version value */
	fu_byte_array_append_uint64(buf, fu_firmware_get_version_raw(firmware), G_LITTLE_ENDIAN);

	/* producer ID */
	if (self->producer_id != NULL) {
		gsize producer_idsz = strlen(self->producer_id);
		if (!fu_memcpy_safe(producer_id,
				    sizeof(producer_id),
				    0x0, /* dst */
				    (const guint8 *)self->producer_id,
				    producer_idsz,
				    0x0, /* src */
				    producer_idsz,
				    error))
			return NULL;
	}
	g_byte_array_append(buf, producer_id, sizeof(producer_id));

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_acpi_phat_version_element_set_guid(FuAcpiPhatVersionElement *self, const gchar *guid)
{
	g_free(self->guid);
	self->guid = g_strdup(guid);
}

static void
fu_acpi_phat_version_element_set_producer_id(FuAcpiPhatVersionElement *self,
					     const gchar *producer_id)
{
	g_free(self->producer_id);
	self->producer_id = g_strdup(producer_id);
}

static gboolean
fu_acpi_phat_version_element_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuAcpiPhatVersionElement *self = FU_ACPI_PHAT_VERSION_ELEMENT(firmware);
	const gchar *tmp;

	/* optional properties */
	tmp = xb_node_query_text(n, "producer_id", NULL);
	if (tmp != NULL)
		fu_acpi_phat_version_element_set_producer_id(self, tmp);
	tmp = xb_node_query_text(n, "guid", NULL);
	if (tmp != NULL)
		fu_acpi_phat_version_element_set_guid(self, tmp);

	/* success */
	return TRUE;
}

static void
fu_acpi_phat_version_element_init(FuAcpiPhatVersionElement *self)
{
}

static void
fu_acpi_phat_version_element_finalize(GObject *object)
{
	FuAcpiPhatVersionElement *self = FU_ACPI_PHAT_VERSION_ELEMENT(object);
	g_free(self->guid);
	g_free(self->producer_id);
	G_OBJECT_CLASS(fu_acpi_phat_version_element_parent_class)->finalize(object);
}

static void
fu_acpi_phat_version_element_class_init(FuAcpiPhatVersionElementClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_acpi_phat_version_element_finalize;
	klass_firmware->parse = fu_acpi_phat_version_element_parse;
	klass_firmware->write = fu_acpi_phat_version_element_write;
	klass_firmware->export = fu_acpi_phat_version_element_export;
	klass_firmware->build = fu_acpi_phat_version_element_build;
}

FuFirmware *
fu_acpi_phat_version_element_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ACPI_PHAT_VERSION_ELEMENT, NULL));
}
