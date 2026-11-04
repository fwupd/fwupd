/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-acpi-facp-struct.h"
#include "fu-acpi-facp.h"

struct _FuAcpiFacp {
	FuAcpiTable parent_instance;
	gboolean s2i;
	FuAcpiFadtPmProfile pm_profile;
};

G_DEFINE_TYPE(FuAcpiFacp, fu_acpi_facp, FU_TYPE_ACPI_TABLE)

static void
fu_acpi_facp_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuAcpiFacp *self = FU_ACPI_FACP(firmware);

	/* FuAcpiTable->export */
	FU_FIRMWARE_CLASS(fu_acpi_facp_parent_class)->export(firmware, flags, bn);

	fu_xmlb_builder_insert_kb(bn, "s2i", self->s2i);
}

static gboolean
fu_acpi_facp_parse(FuFirmware *firmware,
		   FuInputStream *stream,
		   FuFirmwareParseFlags flags,
		   GError **error)
{
	FuAcpiFacp *self = FU_ACPI_FACP(firmware);
	g_autoptr(FuInputStream) stream_payload = NULL;
	g_autoptr(FuStructAcpiFacp) st = NULL;

	/* FuAcpiTable->parse */
	if (!FU_FIRMWARE_CLASS(fu_acpi_facp_parent_class)
		 ->parse(FU_FIRMWARE(self),
			 stream,
			 flags | FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
			 error))
		return FALSE;

	/* check signature and read PM profile and flags */
	if (g_strcmp0(fu_firmware_get_id(FU_FIRMWARE(self)), "FACP") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not a FACP table, got %s",
			    fu_firmware_get_id(FU_FIRMWARE(self)));
		return FALSE;
	}

	stream_payload = fu_acpi_table_get_payload(FU_ACPI_TABLE(self), error);
	if (stream_payload == NULL)
		return FALSE;
	st = fu_struct_acpi_facp_parse_stream(stream_payload, 0x0, error);
	if (st == NULL)
		return FALSE;
	self->pm_profile = fu_struct_acpi_facp_get_pm_profile(st);
	self->s2i =
	    (fu_struct_acpi_facp_get_flags(st) & FU_ACPI_FACP_FLAG_LOW_POWER_S0_IDLE_CAPABLE) > 0;
	return TRUE;
}

static GByteArray *
fu_acpi_facp_write(FuFirmware *firmware, GError **error)
{
	FuAcpiFacp *self = FU_ACPI_FACP(firmware);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FwupdMemoryInputStream) stream_payload = NULL;
	g_autoptr(FuStructAcpiFacp) st = fu_struct_acpi_facp_new();

	/* PM profile and flags */
	fu_struct_acpi_facp_set_pm_profile(st, self->pm_profile);
	if (self->s2i)
		fu_struct_acpi_facp_set_flags(st, FU_ACPI_FACP_FLAG_LOW_POWER_S0_IDLE_CAPABLE);

	/* FuAcpiTable->write */
	blob = fu_struct_acpi_facp_to_bytes(st);
	stream_payload = fwupd_memory_input_stream_new_from_bytes(blob);
	fu_acpi_table_set_payload(FU_ACPI_TABLE(self), FWUPD_INPUT_STREAM(stream_payload));
	return FU_FIRMWARE_CLASS(fu_acpi_facp_parent_class)->write(firmware, error);
}

static gboolean
fu_acpi_facp_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuAcpiFacp *self = FU_ACPI_FACP(firmware);
	const gchar *tmp;

	/* optional properties */
	tmp = xb_node_query_text(n, "s2i", NULL);
	if (tmp != NULL) {
		if (!fu_strtobool(tmp, &self->s2i, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

FuAcpiFacp *
fu_acpi_facp_new(void)
{
	return g_object_new(FU_TYPE_ACPI_FACP, NULL);
}

gboolean
fu_acpi_facp_get_s2i(FuAcpiFacp *self)
{
	g_return_val_if_fail(FU_IS_ACPI_FACP(self), FALSE);
	return self->s2i;
}

void
fu_acpi_facp_set_s2i(FuAcpiFacp *self, gboolean s2i)
{
	g_return_if_fail(FU_IS_ACPI_FACP(self));
	self->s2i = s2i;
}

FuAcpiFadtPmProfile
fu_acpi_facp_get_pm_profile(FuAcpiFacp *self)
{
	g_return_val_if_fail(FU_IS_ACPI_FACP(self), FU_ACPI_FADT_PM_PROFILE_UNSPECIFIED);
	return self->pm_profile;
}

void
fu_acpi_facp_set_pm_profile(FuAcpiFacp *self, FuAcpiFadtPmProfile pm_profile)
{
	g_return_if_fail(FU_IS_ACPI_FACP(self));
	self->pm_profile = pm_profile;
}

static void
fu_acpi_facp_class_init(FuAcpiFacpClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->export = fu_acpi_facp_export;
	firmware_class->parse = fu_acpi_facp_parse;
	firmware_class->write = fu_acpi_facp_write;
	firmware_class->build = fu_acpi_facp_build;
}

static void
fu_acpi_facp_init(FuAcpiFacp *self)
{
	fu_firmware_set_id(FU_FIRMWARE(self), "FACP");
}
