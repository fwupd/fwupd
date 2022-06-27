/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-nordic-hid-archive.h"
#include "fu-nordic-hid-firmware-b0.h"
#include "fu-nordic-hid-firmware-mcuboot.h"

/* current version format is 0 */
#define MAX_VERSION_FORMAT 0

struct _FuNordicHidArchive {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuNordicHidArchive, fu_nordic_hid_archive, FU_TYPE_FIRMWARE)

static gboolean
fu_nordic_hid_archive_parse(FuFirmware *firmware,
			    GBytes *fw,
			    gsize offset,
			    FwupdInstallFlags flags,
			    GError **error)
{
	JsonNode *json_root_node;
	JsonObject *json_obj;
	JsonArray *json_files;
	guint manifest_ver;
	guint files_cnt = 0;
	GBytes *manifest = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(JsonParser) parser = json_parser_new();

	/* load archive */
	archive = fu_archive_new(fw, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;
	manifest = fu_archive_lookup_by_fn(archive, "manifest.json", error);
	if (manifest == NULL)
		return FALSE;

	/* parse JSON */
	if (!json_parser_load_from_data(parser,
					(const gchar *)g_bytes_get_data(manifest, NULL),
					(gssize)g_bytes_get_size(manifest),
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

	manifest_ver = json_object_get_int_member(json_obj, "format-version");
	if (manifest_ver > MAX_VERSION_FORMAT) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "unsupported manifest version");
		return FALSE;
	}

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
		const gchar *filename = NULL;
		const gchar *bootloader_name = NULL;
		guint image_addr = 0;
		JsonObject *obj = json_array_get_object_element(json_files, i);
		GBytes *blob = NULL;
		g_autoptr(FuFirmware) image = NULL;
		g_autofree gchar *image_id = NULL;
		g_auto(GStrv) board_split = NULL;

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

		if (json_object_has_member(obj, "version_B0")) {
			bootloader_name = "B0";
			image = g_object_new(FU_TYPE_NORDIC_HID_FIRMWARE_B0, NULL);
		} else if (json_object_has_member(obj, "version_MCUBOOT")) {
			bootloader_name = "MCUBOOT";
			image = g_object_new(FU_TYPE_NORDIC_HID_FIRMWARE_MCUBOOT, NULL);
		} else {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "only B0 and MCUboot bootloaders are supported");
			return FALSE;
		}

		/* the "board" field contains board name before "_" symbol */
		if (!json_object_has_member(obj, "board")) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "manifest invalid as has no target board information");
			return FALSE;
		}
		board_split = g_strsplit(json_object_get_string_member(obj, "board"), "_", -1);
		if (board_split[0] == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "manifest invalid as has no target board information");
			return FALSE;
		}
		/* images for B0 bootloader are listed in strict order:
		 * this is guaranteed by producer set the id format as
		 * <board>_<bl>_<bank>N, i.e "nrf52840dk_B0_bank0".
		 * For MCUBoot bootloader only the single image is available */
		image_id = g_strdup_printf("%s_%s_bank%01u", board_split[0], bootloader_name, i);
		if (!fu_firmware_parse(image, blob, flags, error))
			return FALSE;

		fu_firmware_set_id(image, image_id);
		fu_firmware_set_idx(image, i);
		if (json_object_has_member(obj, "load_address")) {
			image_addr = json_object_get_int_member(obj, "load_address");
			fu_firmware_set_addr(image, image_addr);
		}
		fu_firmware_add_image(firmware, image);
	}

	/* success */
	return TRUE;
}

static void
fu_nordic_hid_archive_init(FuNordicHidArchive *self)
{
}

static void
fu_nordic_hid_archive_class_init(FuNordicHidArchiveClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_nordic_hid_archive_parse;
}
