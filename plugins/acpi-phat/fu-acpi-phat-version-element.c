/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

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
	FuStruct *st = fu_struct_lookup(self, "PhatVersionElement");
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* unpack */
	if (!fu_struct_unpack_full(st, buf, bufsz, offset, FU_STRUCT_FLAG_NONE, error))
		return FALSE;
	fu_firmware_set_size(firmware, fu_struct_size(st));
	self->guid = fwupd_guid_to_string(fu_struct_get_guid(st, "component_id"),
					  FWUPD_GUID_FLAG_MIXED_ENDIAN);
	self->producer_id = fu_struct_get_string(st, "producer_id");
	fu_firmware_set_version_raw(firmware, fu_struct_get_u64(st, "version_value"));
	return TRUE;
}

static GBytes *
fu_acpi_phat_version_element_write(FuFirmware *firmware, GError **error)
{
	FuAcpiPhatVersionElement *self = FU_ACPI_PHAT_VERSION_ELEMENT(firmware);
	FuStruct *st = fu_struct_lookup(self, "PhatVersionElement");

	/* pack */
	if (self->guid != NULL) {
		fwupd_guid_t guid = {0x0};
		if (!fwupd_guid_from_string(self->guid, &guid, FWUPD_GUID_FLAG_MIXED_ENDIAN, error))
			return NULL;
		fu_struct_set_guid(st, "component_id", &guid);
	}
	fu_struct_set_u64(st, "version_value", fu_firmware_get_version_raw(firmware));
	if (!fu_struct_set_string(st, "producer_id", self->producer_id, error))
		return NULL;

	/* success */
	return fu_struct_pack_bytes(st);
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
	fu_struct_register(self,
			   "PhatVersionElement {"
			   "    component_id: guid,"
			   "    version_value: u64le,"
			   "    producer_id: 4s,"
			   "}");
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
