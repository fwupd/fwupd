/*
 * Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-telink-dfu-archive.h"
#include "fu-telink-dfu-common.h"

struct _FuTelinkDfuArchive {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuTelinkDfuArchive, fu_telink_dfu_archive, FU_TYPE_FIRMWARE)

#define FU_TELINK_DFU_FIRMWARE_JSON_FORMAT_VERSION_MAX 0

static gboolean
fu_telink_dfu_archive_load_file(FuTelinkDfuArchive *self,
				FuFirmware *archive,
				FwupdJsonObject *json_obj,
				guint i,
				FuFirmwareParseFlags flags,
				GError **error)
{
	struct {
		const gchar *name;
		FwupdVersionFormat ver_format;
	} bl_type_keys[] = {
	    {"beta", FWUPD_VERSION_FORMAT_TRIPLET},
	    {"ota-v1", FWUPD_VERSION_FORMAT_TRIPLET},
	    {"usb-dongle-simple", FWUPD_VERSION_FORMAT_PAIR},
	};
	const gchar *board_name;
	const gchar *bootloader_name;
	const gchar *filename;
	const gchar *version;
	gboolean supported_bootloader = FALSE;
	gint64 image_addr = 0;
	g_autofree gchar *image_id = NULL;
	g_autoptr(FuFirmware) image = fu_firmware_new();
	g_autoptr(GBytes) blob = NULL;
	guint bl_type_idx;

	filename = fwupd_json_object_get_string(json_obj, "file", error);
	if (filename == NULL)
		return FALSE;
	blob = fu_firmware_get_image_by_id_bytes(archive, filename, error);
	if (blob == NULL)
		return FALSE;

	bootloader_name = fwupd_json_object_get_string(json_obj, "bootloader_type", error);
	if (bootloader_name == NULL)
		return FALSE;
	for (bl_type_idx = 0; bl_type_idx < sizeof(bl_type_keys) / sizeof(bl_type_keys[0]);
	     bl_type_idx++) {
		if (g_strcmp0(bootloader_name, bl_type_keys[bl_type_idx].name) == 0) {
			supported_bootloader = TRUE;
			break;
		}
	}
	if (!supported_bootloader) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "bad param: bootloader_type");
		return FALSE;
	}

	board_name = fwupd_json_object_get_string(json_obj, "board", error);
	if (board_name == NULL)
		return FALSE;

	/* string format: <board_name>_<bootloader_name>_<bank>N
	 *   will compare 'image_id' in fu_firmware_get_image_by_id()
	 *   e.g. 8278_otav1_bank0 */
	image_id = g_strdup_printf("%s_%s_bank%01u", board_name, bootloader_name, i);
	if (!fu_firmware_parse_bytes(image, blob, 0x0, flags, error))
		return FALSE;
	g_debug("image_id=%s", image_id);

	fu_firmware_set_id(image, image_id);
	fu_firmware_set_idx(image, i);
	if (!fwupd_json_object_get_integer_with_default(json_obj,
							"load_address",
							&image_addr,
							-1,
							error))
		return FALSE;
	if (image_addr != -1)
		fu_firmware_set_addr(image, image_addr);
	if (!fu_firmware_add_image(FU_FIRMWARE(self), image, error))
		return FALSE;

	version = fwupd_json_object_get_string(json_obj, "image_version", error);
	if (version == NULL)
		return FALSE;
	fu_firmware_set_version_raw(
	    FU_FIRMWARE(self),
	    fu_telink_dfu_parse_image_version(version, bl_type_keys[bl_type_idx].ver_format));
	fu_firmware_set_version(FU_FIRMWARE(self), version); /* nocheck:set-version */

	/* success */
	return TRUE;
}

static gboolean
fu_telink_dfu_archive_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    FuFirmwareParseFlags flags,
			    GError **error)
{
	FuTelinkDfuArchive *self = FU_TELINK_DFU_ARCHIVE(firmware);
	g_autoptr(FwupdJsonArray) json_files = NULL;
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	gint64 manifest_ver = 0;
	g_autoptr(FuFirmware) archive = fu_zip_firmware_new();
	g_autoptr(GBytes) manifest = NULL;
	g_autoptr(FwupdJsonParser) parser = fwupd_json_parser_new();

	/* load archive */
	if (!fu_firmware_parse_stream(archive, stream, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, error))
		return FALSE;

	/* parse manifest.json */
	manifest = fu_firmware_get_image_by_id_bytes(archive, "manifest.json", error);
	if (manifest == NULL)
		return FALSE;
	json_node =
	    fwupd_json_parser_load_from_bytes(parser, manifest, FWUPD_JSON_LOAD_FLAG_NONE, error);
	if (json_node == NULL) {
		g_prefix_error_literal(error, "manifest not in JSON format: ");
		return FALSE;
	}
	json_obj = fwupd_json_node_get_object(json_node, error);
	if (json_obj == NULL)
		return FALSE;
	if (!fwupd_json_object_has_node(json_obj, "format-version")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "manifest has invalid format");
		return FALSE;
	}

	/* maximum-allowed format version(backward compatibility) */
	if (!fwupd_json_object_get_integer_with_default(json_obj,
							"format-version",
							&manifest_ver,
							0,
							error))
		return FALSE;
	if (manifest_ver > FU_TELINK_DFU_FIRMWARE_JSON_FORMAT_VERSION_MAX) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "unsupported manifest version");
		return FALSE;
	}
	g_debug("manifest_ver=0x%u", (guint)manifest_ver);

	/* get files */
	json_files = fwupd_json_object_get_array(json_obj, "files", error);
	if (json_files == NULL)
		return FALSE;
	if (fwupd_json_array_get_size(json_files) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "manifest invalid as contains no update images");
		return FALSE;
	}
	for (guint i = 0; i < fwupd_json_array_get_size(json_files); i++) {
		g_autoptr(FwupdJsonObject) json_object_file = NULL;
		json_object_file = fwupd_json_array_get_object(json_files, i, error);
		if (json_object_file == NULL)
			return FALSE;
		if (!fu_telink_dfu_archive_load_file(self,
						     archive,
						     json_object_file,
						     i,
						     flags,
						     error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_telink_dfu_archive_init(FuTelinkDfuArchive *self)
{
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_FIRMWARE);
}

static void
fu_telink_dfu_archive_class_init(FuTelinkDfuArchiveClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_telink_dfu_archive_parse;
}
