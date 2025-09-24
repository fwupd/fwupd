/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEfiVssAuthVariable"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-efi-common.h"
#include "fu-efi-struct.h"
#include "fu-efi-vss-auth-variable.h"
#include "fu-string.h"

/**
 * FuEfiVssAuthVariable:
 *
 * A NVRAM authenticated variable
 *
 * See also: [class@FuFirmware]
 */

struct _FuEfiVssAuthVariable {
	FuFirmware parent_instance;
	gchar *vendor_guid;
	FuEfiVariableAttributes attributes;
	FuEfiVariableState state;
	FuStructEfiTime *timestamp; /* nullable */
};

G_DEFINE_TYPE(FuEfiVssAuthVariable, fu_efi_vss_auth_variable, FU_TYPE_FIRMWARE)

static void
fu_efi_vss_auth_variable_export(FuFirmware *firmware,
				FuFirmwareExportFlags flags,
				XbBuilderNode *bn)
{
	FuEfiVssAuthVariable *self = FU_EFI_VSS_AUTH_VARIABLE(firmware);
	fu_xmlb_builder_insert_kv(bn, "vendor_guid", self->vendor_guid);
	if (self->state != FU_EFI_VARIABLE_STATE_UNSET) {
		const gchar *str = fu_efi_variable_state_to_string(self->state);
		fu_xmlb_builder_insert_kv(bn, "state", str);
	}
	if (self->attributes != FU_EFI_VARIABLE_ATTRIBUTES_NONE) {
		g_autofree gchar *str = fu_efi_variable_attributes_to_string(self->attributes);
		fu_xmlb_builder_insert_kv(bn, "attributes", str);
	}
	if (self->timestamp != NULL) {
		g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "timestamp", NULL);
		fu_efi_timestamp_export(self->timestamp, bc);
	}
}

static gboolean
fu_efi_vss_auth_variable_parse(FuFirmware *firmware,
			       GInputStream *stream,
			       FuFirmwareParseFlags flags,
			       GError **error)
{
	FuEfiVssAuthVariable *self = FU_EFI_VSS_AUTH_VARIABLE(firmware);
	gsize offset = 0x0;
	g_autoptr(FuStructEfiVssAuthVariableHeader) st = NULL;
	g_autoptr(GByteArray) buf_name = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autofree gchar *name = NULL;

	st = fu_struct_efi_vss_auth_variable_header_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_efi_vss_auth_variable_header_get_start_id(st) == 0xFFFF) {
		fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_IS_LAST_IMAGE);
		return TRUE;
	}
	if (fu_struct_efi_vss_auth_variable_header_get_start_id(st) !=
	    FU_STRUCT_EFI_VSS_AUTH_VARIABLE_HEADER_DEFAULT_START_ID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid VSS variable start ID, expected 0x%x and got 0x%x",
			    (guint)FU_STRUCT_EFI_VSS_AUTH_VARIABLE_HEADER_DEFAULT_START_ID,
			    fu_struct_efi_vss_auth_variable_header_get_start_id(st));
		return FALSE;
	}

	/* attributes we care about */
	self->vendor_guid =
	    fwupd_guid_to_string(fu_struct_efi_vss_auth_variable_header_get_vendor_guid(st),
				 FWUPD_GUID_FLAG_MIXED_ENDIAN);
	self->attributes = fu_struct_efi_vss_auth_variable_header_get_attributes(st);
	self->state = fu_struct_efi_vss_auth_variable_header_get_state(st);
	self->timestamp = fu_struct_efi_vss_auth_variable_header_get_timestamp(st);

	/* read name */
	offset += st->len;
	buf_name = fu_input_stream_read_byte_array(
	    stream,
	    offset,
	    fu_struct_efi_vss_auth_variable_header_get_name_size(st),
	    NULL,
	    error);
	if (buf_name == NULL)
		return FALSE;
	name = fu_utf16_to_utf8_byte_array(buf_name, G_LITTLE_ENDIAN, error);
	if (name == NULL)
		return FALSE;
	fu_firmware_set_id(firmware, name);

	/* read data */
	offset += fu_struct_efi_vss_auth_variable_header_get_name_size(st);
	data = fu_input_stream_read_bytes(stream,
					  offset,
					  fu_struct_efi_vss_auth_variable_header_get_data_size(st),
					  NULL,
					  error);
	if (data == NULL)
		return FALSE;
	fu_firmware_set_bytes(firmware, data);

	/* next header */
	offset += fu_struct_efi_vss_auth_variable_header_get_data_size(st);

	/* success */
	fu_firmware_set_size(firmware, offset);
	return TRUE;
}

static GByteArray *
fu_efi_vss_auth_variable_write(FuFirmware *firmware, GError **error)
{
	FuEfiVssAuthVariable *self = FU_EFI_VSS_AUTH_VARIABLE(firmware);
	g_autoptr(FuStructEfiVssAuthVariableHeader) st =
	    fu_struct_efi_vss_auth_variable_header_new();
	g_autoptr(GBytes) name = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* attrs */
	fu_struct_efi_vss_auth_variable_header_set_attributes(st, self->attributes);
	fu_struct_efi_vss_auth_variable_header_set_state(st, self->state);
	if (self->timestamp != NULL) {
		if (!fu_struct_efi_vss_auth_variable_header_set_timestamp(st,
									  self->timestamp,
									  error))
			return NULL;
	}

	/* name */
	name = fu_utf8_to_utf16_bytes(fu_firmware_get_id(firmware),
				      G_LITTLE_ENDIAN,
				      FU_UTF_CONVERT_FLAG_APPEND_NUL,
				      error);
	if (name == NULL)
		return NULL;
	fu_struct_efi_vss_auth_variable_header_set_name_size(st, g_bytes_get_size(name));

	/* data */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return NULL;
	fu_struct_efi_vss_auth_variable_header_set_data_size(st, g_bytes_get_size(blob));

	/* guid */
	if (self->vendor_guid != NULL) {
		fwupd_guid_t guid = {0};
		if (!fwupd_guid_from_string(self->vendor_guid,
					    &guid,
					    FWUPD_GUID_FLAG_MIXED_ENDIAN,
					    error))
			return NULL;
		fu_struct_efi_vss_auth_variable_header_set_vendor_guid(st, &guid);
	}

	/* concat */
	fu_byte_array_append_bytes(st, name);
	fu_byte_array_append_bytes(st, blob);

	/* success */
	return g_steal_pointer(&st);
}

static gboolean
fu_efi_vss_auth_variable_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEfiVssAuthVariable *self = FU_EFI_VSS_AUTH_VARIABLE(firmware);
	const gchar *tmp;
	g_autoptr(XbNode) n_timestamp = NULL;

	/* simple properties */
	tmp = xb_node_query_text(n, "vendor_guid", NULL);
	if (tmp != NULL)
		self->vendor_guid = g_strdup(tmp);
	tmp = xb_node_query_text(n, "attributes", NULL);
	if (tmp != NULL)
		self->attributes = fu_efi_variable_attributes_from_string(tmp);
	tmp = xb_node_query_text(n, "state", NULL);
	if (tmp != NULL)
		self->state = fu_efi_variable_state_from_string(tmp);

	/* EFI_TIME */
	n_timestamp = xb_node_query_first(n, "timestamp", NULL);
	if (n_timestamp != NULL) {
		self->timestamp = fu_struct_efi_time_new();
		fu_efi_timestamp_build(self->timestamp, n_timestamp);
	}

	/* success */
	return TRUE;
}

static void
fu_efi_vss_auth_variable_init(FuEfiVssAuthVariable *self)
{
}

static void
fu_efi_vss_auth_variable_finalize(GObject *obj)
{
	FuEfiVssAuthVariable *self = FU_EFI_VSS_AUTH_VARIABLE(obj);
	if (self->timestamp != NULL)
		fu_struct_efi_time_unref(self->timestamp);
	g_free(self->vendor_guid);
	G_OBJECT_CLASS(fu_efi_vss_auth_variable_parent_class)->finalize(obj);
}

static void
fu_efi_vss_auth_variable_class_init(FuEfiVssAuthVariableClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_efi_vss_auth_variable_finalize;
	firmware_class->parse = fu_efi_vss_auth_variable_parse;
	firmware_class->export = fu_efi_vss_auth_variable_export;
	firmware_class->write = fu_efi_vss_auth_variable_write;
	firmware_class->build = fu_efi_vss_auth_variable_build;
}

/**
 * fu_efi_vss_auth_variable_new:
 *
 * Creates an empty VSS variable store.
 *
 * Returns: a #FuFirmware
 *
 * Since: 2.0.17
 **/
FuFirmware *
fu_efi_vss_auth_variable_new(void)
{
	return g_object_new(FU_TYPE_EFI_VSS_AUTH_VARIABLE, NULL);
}
