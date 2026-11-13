/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-common.h"
#include "fu-efi-common.h"
#include "fu-efi-section.h"
#include "fu-input-stream.h"
#include "fu-partial-input-stream.h"

/**
 * fu_efi_guid_to_name:
 * @guid: A lowercase GUID string, e.g. `8c8ce578-8a3d-4f1c-9935-896185c32dd3`
 *
 * Converts a GUID to the known nice name.
 *
 * Returns: identifier string, or %NULL if unknown
 *
 * Since: 1.6.2
 **/
const gchar *
fu_efi_guid_to_name(const gchar *guid)
{
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_FFS1) == 0)
		return "Volume:Ffs1";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_FFS2) == 0)
		return "Volume:Ffs2";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_FFS3) == 0)
		return "Volume:Ffs3";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_NVRAM_EVSA) == 0)
		return "Volume:NvramEvsa";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_NVRAM_NVAR) == 0)
		return "Volume:NvramNvar";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_NVRAM_EVSA2) == 0)
		return "Volume:NvramEvsa2";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_APPLE_BOOT) == 0)
		return "Volume:AppleBoot";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_PFH1) == 0)
		return "Volume:Pfh1";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_PFH2) == 0)
		return "Volume:Pfh2";
	if (g_strcmp0(guid, FU_EFI_VOLUME_GUID_HP_FS) == 0)
		return "Volume:HpFs";
	if (g_strcmp0(guid, FU_EFI_FILE_GUID_FV_IMAGE) == 0)
		return "File:FvImage";
	if (g_strcmp0(guid, FU_EFI_FILE_GUID_MICROCODE) == 0)
		return "File:Microcode";
	if (g_strcmp0(guid, FU_EFI_FILE_GUID_BIOS_GUARD) == 0)
		return "File:BiosGuard";
	if (g_strcmp0(guid, FU_EFI_SECTION_GUID_LZMA_COMPRESS) == 0)
		return "Section:LzmaCompress";
	if (g_strcmp0(guid, FU_EFI_SECTION_GUID_TIANO_COMPRESS) == 0)
		return "Section:TianoCompress";
	if (g_strcmp0(guid, FU_EFI_SECTION_GUID_SMBIOS_TABLE) == 0)
		return "Section:SmbiosTable";
	if (g_strcmp0(guid, FU_EFI_SECTION_GUID_ESRT_TABLE) == 0)
		return "Section:EsrtTable";
	if (g_strcmp0(guid, FU_EFI_SECTION_GUID_ACPI1_TABLE) == 0)
		return "Section:Acpi1Table";
	if (g_strcmp0(guid, FU_EFI_SECTION_GUID_ACPI2_TABLE) == 0)
		return "Section:Acpi2Table";
	return NULL;
}

/**
 * fu_efi_parse_sections:
 * @firmware: #FuFirmware
 * @stream: a #FuInputStream
 * @flags: #FuFirmwareParseFlags
 * @error: (nullable): optional return location for an error
 *
 * Parses a UEFI section.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efi_parse_sections(FuFirmware *firmware,
		      FuInputStream *stream,
		      gsize offset,
		      FuFirmwareParseFlags flags,
		      GError **error)
{
	gsize streamsz = 0;

	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);
	g_return_val_if_fail(FWUPD_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	while (offset < streamsz) {
		g_autoptr(FuFirmware) img = fu_efi_section_new();
		g_autoptr(FuInputStream) partial_stream = NULL;

		/* parse maximum payload */
		partial_stream =
		    fu_partial_input_stream_new(stream, offset, streamsz - offset, error);
		if (partial_stream == NULL) {
			g_prefix_error_literal(error, "failed to cut payload: ");
			return FALSE;
		}
		if (!fu_firmware_parse_stream(img,
					      partial_stream,
					      0x0,
					      flags | FU_FIRMWARE_PARSE_FLAG_NO_SEARCH,
					      error)) {
			g_prefix_error(error,
				       "failed to parse section of size 0x%x: ",
				       (guint)streamsz);
			return FALSE;
		}

		/* invalid */
		if (fu_firmware_get_size(img) == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "section had zero size");
			return FALSE;
		}

		fu_firmware_set_offset(img, offset);
		if (!fu_firmware_add_image(firmware, img, error))
			return FALSE;

		/* next! */
		if (!fu_size_checked_inc(
			&offset,
			fu_common_align_up(fu_firmware_get_size(img), FU_FIRMWARE_ALIGNMENT_4),
			error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_efi_timestamp_export:
 * @st: a #FuStructEfiTime
 * @json_object: a #FwupdJsonObject
 *
 * Exports an `EFI_TIME` to JSON.
 *
 * Since: 2.0.17
 **/
void
fu_efi_timestamp_export(FuStructEfiTime *st, FwupdJsonObject *json_object)
{
	if (fu_struct_efi_time_get_year(st) != 0)
		fwupd_json_object_add_integer(json_object, "year", fu_struct_efi_time_get_year(st));
	if (fu_struct_efi_time_get_month(st) != 0)
		fwupd_json_object_add_integer(json_object,
					      "month",
					      fu_struct_efi_time_get_month(st));
	if (fu_struct_efi_time_get_day(st) != 0)
		fwupd_json_object_add_integer(json_object, "day", fu_struct_efi_time_get_day(st));
	if (fu_struct_efi_time_get_hour(st) != 0)
		fwupd_json_object_add_integer(json_object, "hour", fu_struct_efi_time_get_hour(st));
	if (fu_struct_efi_time_get_minute(st) != 0)
		fwupd_json_object_add_integer(json_object,
					      "minute",
					      fu_struct_efi_time_get_minute(st));
	if (fu_struct_efi_time_get_second(st) != 0)
		fwupd_json_object_add_integer(json_object,
					      "second",
					      fu_struct_efi_time_get_second(st));
}

/* sets @value to G_MAXINT64 if @key is not found */
static gboolean
fu_efi_timestamp_build_value(FwupdJsonObject *json_object,
			     const gchar *key,
			     gint64 value_max,
			     gint64 *value,
			     GError **error)
{
	if (!fwupd_json_object_get_integer_with_default(json_object,
							key,
							value,
							G_MAXINT64,
							error)) {
		g_prefix_error(error, "failed to parse %s: ", key);
		return FALSE;
	}
	if (*value == G_MAXINT64)
		return TRUE;
	if (*value < 0 || *value > value_max) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "%s value %" G_GINT64_FORMAT " invalid, maximum is %" G_GINT64_FORMAT,
			    key,
			    *value,
			    value_max);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_efi_timestamp_build:
 * @st: a #FuStructEfiTime
 * @json_object: a #FwupdJsonObject
 * @error: (nullable): optional return location for an error
 *
 * Imports an `EFI_TIME` from JSON. Missing members are ignored.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.17
 **/
gboolean
fu_efi_timestamp_build(FuStructEfiTime *st, FwupdJsonObject *json_object, GError **error)
{
	gint64 tmp = G_MAXINT64;

	g_return_val_if_fail(st != NULL, FALSE);
	g_return_val_if_fail(json_object != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_efi_timestamp_build_value(json_object, "year", G_MAXUINT16, &tmp, error))
		return FALSE;
	if (tmp != G_MAXINT64)
		fu_struct_efi_time_set_year(st, (guint16)tmp);
	if (!fu_efi_timestamp_build_value(json_object, "month", G_MAXUINT8, &tmp, error))
		return FALSE;
	if (tmp != G_MAXINT64)
		fu_struct_efi_time_set_month(st, (guint8)tmp);
	if (!fu_efi_timestamp_build_value(json_object, "day", G_MAXUINT8, &tmp, error))
		return FALSE;
	if (tmp != G_MAXINT64)
		fu_struct_efi_time_set_day(st, (guint8)tmp);
	if (!fu_efi_timestamp_build_value(json_object, "hour", G_MAXUINT8, &tmp, error))
		return FALSE;
	if (tmp != G_MAXINT64)
		fu_struct_efi_time_set_hour(st, (guint8)tmp);
	if (!fu_efi_timestamp_build_value(json_object, "minute", G_MAXUINT8, &tmp, error))
		return FALSE;
	if (tmp != G_MAXINT64)
		fu_struct_efi_time_set_minute(st, (guint8)tmp);
	if (!fu_efi_timestamp_build_value(json_object, "second", G_MAXUINT8, &tmp, error))
		return FALSE;
	if (tmp != G_MAXINT64)
		fu_struct_efi_time_set_second(st, (guint8)tmp);

	/* success */
	return TRUE;
}
