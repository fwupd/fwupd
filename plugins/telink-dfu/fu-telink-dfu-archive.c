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
				FuArchive *archive,
				JsonObject *obj,
				guint i,
				FwupdInstallFlags flags,
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
	g_autofree gchar *image_id = NULL;
	g_autoptr(FuFirmware) image = fu_firmware_new();
	g_autoptr(GBytes) blob = NULL;
	guint bl_type_idx;

	if (!json_object_has_member(obj, "file")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "manifest invalid as has no file name for the image");
		return FALSE;
	}
	filename = json_object_get_string_member(obj, "file");
	blob = fu_archive_lookup_by_fn(archive, filename, error);
	if (blob == NULL)
		return FALSE;

	if (!json_object_has_member(obj, "bootloader_type")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "missing param: bootloader_type");
		return FALSE;
	}
	bootloader_name = json_object_get_string_member(obj, "bootloader_type");
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

	if (!json_object_has_member(obj, "board")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "missing param: board");
		return FALSE;
	}
	board_name = json_object_get_string_member(obj, "board");

	/* string format: <board_name>_<bootloader_name>_<bank>N
	 *   will compare 'image_id' in fu_firmware_get_image_by_id()
	 *   e.g. 8278_otav1_bank0 */
	image_id = g_strdup_printf("%s_%s_bank%01u", board_name, bootloader_name, i);
	if (!fu_firmware_parse_bytes(image, blob, 0x0, flags, error))
		return FALSE;
	g_debug("image_id=%s", image_id);

	fu_firmware_set_id(image, image_id);
	fu_firmware_set_idx(image, i);
	if (json_object_has_member(obj, "load_address")) {
		guint image_addr = json_object_get_int_member(obj, "load_address");
		fu_firmware_set_addr(image, image_addr);
	}
	fu_firmware_add_image(FU_FIRMWARE(self), image);

	if (!json_object_has_member(obj, "image_version")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "missing param: image_version");
		return FALSE;
	}
	version = json_object_get_string_member(obj, "image_version");
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
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuTelinkDfuArchive *self = FU_TELINK_DFU_ARCHIVE(firmware);
	JsonArray *json_files;
	JsonNode *json_root_node;
	JsonObject *json_obj;
	guint files_cnt = 0;
	guint manifest_ver;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) manifest = NULL;
	g_autoptr(JsonParser) parser = json_parser_new();

	/* load archive */
	archive = fu_archive_new_stream(stream, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;

	/* parse manifest.json */
	manifest = fu_archive_lookup_by_fn(archive, "manifest.json", error);
	if (manifest == NULL)
		return FALSE;
	if (!json_parser_load_from_data(parser,
					g_bytes_get_data(manifest, NULL),
					g_bytes_get_size(manifest),
					error)) {
		g_prefix_error(error, "manifest not in JSON format: ");
		return FALSE;
	}
	json_root_node = json_parser_get_root(parser);
	if (json_root_node == NULL || !JSON_NODE_HOLDS_OBJECT(json_root_node)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "manifest invalid as has no root");
		return FALSE;
	}
	json_obj = json_node_get_object(json_root_node);
	if (!json_object_has_member(json_obj, "format-version")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "manifest has invalid format");
		return FALSE;
	}

	/* maximum-allowed format version(backward compatibility) */
	manifest_ver = json_object_get_int_member(json_obj, "format-version");
	if (manifest_ver > FU_TELINK_DFU_FIRMWARE_JSON_FORMAT_VERSION_MAX) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "unsupported manifest version");
		return FALSE;
	}
	g_debug("manifest_ver=0x%u", manifest_ver);

	/* get files */
	json_files = json_object_get_array_member(json_obj, "files");
	if (json_files == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "manifest invalid as has no 'files' array");
		return FALSE;
	}
	files_cnt = json_array_get_length(json_files);
	if (files_cnt == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "manifest invalid as contains no update images");
		return FALSE;
	}
	for (guint i = 0; i < files_cnt; i++) {
		JsonObject *obj = json_array_get_object_element(json_files, i);
		if (!fu_telink_dfu_archive_load_file(self, archive, obj, i, flags, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_telink_dfu_archive_init(FuTelinkDfuArchive *self)
{
}

static void
fu_telink_dfu_archive_class_init(FuTelinkDfuArchiveClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_telink_dfu_archive_parse;
}
