/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-nordic-hid-archive.h"
#include "fu-nordic-hid-firmware-b0.h"
#include "fu-nordic-hid-firmware-mcuboot.h"

/* the plugin currently supports version format of either 0 or 1 */
#define MIN_VERSION_FORMAT 0
#define MAX_VERSION_FORMAT 1

struct _FuNordicHidArchive {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuNordicHidArchive, fu_nordic_hid_archive, FU_TYPE_FIRMWARE)

static const gchar *
fu_nordic_hid_archive_parse_file_get_bootloader_name(JsonObject *obj, GError **error)
{
	if (json_object_has_member(obj, "version_B0"))
		return "B0";
	if (json_object_has_member(obj, "version_MCUBOOT"))
		return "MCUBOOT";
	if (json_object_has_member(obj, "version_MCUBOOT+XIP"))
		return "MCUBOOT+XIP";

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "only B0 and MCUboot bootloaders are supported");
	return NULL;
}

static FuFirmware *
fu_nordic_hid_archive_parse_file_image_create(const gchar *bootloader_name, GError **error)
{
	if (g_strcmp0(bootloader_name, "B0") == 0)
		return g_object_new(FU_TYPE_NORDIC_HID_FIRMWARE_B0, NULL);
	if (g_strcmp0(bootloader_name, "MCUBOOT") == 0 ||
	    g_strcmp0(bootloader_name, "MCUBOOT+XIP") == 0)
		return g_object_new(FU_TYPE_NORDIC_HID_FIRMWARE_MCUBOOT, NULL);

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "only B0 and MCUboot bootloaders are supported");
	return NULL;
}

static gchar *
fu_nordic_hid_archive_parse_file_get_board_name(JsonObject *obj,
						gint64 manifest_ver,
						GError **error)
{
	g_auto(GStrv) board_split = NULL;
	const gchar *board_name_readout = NULL;

	if (!json_object_has_member(obj, "board")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "manifest invalid as has no target board information");
		return NULL;
	}

	board_name_readout = json_object_get_string_member(obj, "board");
	if (board_name_readout == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "manifest invalid as has no target board information");
		return NULL;
	}

	if (manifest_ver == 0) {
		/* for manifest "format-version" of "0", the board name is represented only
		 * by part of the string before the "_" symbol
		 */
		board_split = g_strsplit(board_name_readout, "_", -1);
		if (board_split[0] == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "manifest invalid as has no board information");
			return NULL;
		}
		return g_strdup(board_split[0]);
	}

	/* duplicate string for consistent memory management */
	return g_strdup(board_name_readout);
}

static gboolean
fu_nordic_hid_archive_parse_file_get_flash_area_id_v1(JsonObject *obj,
						      guint *flash_area_id,
						      const gchar *bootloader_name,
						      guint files_cnt,
						      GError **error)
{
	const gchar *image_idx_str = NULL;
	const gchar *slot_str = NULL;
	gint64 image_idx = -1;
	gint64 slot = -1;

	/* for MCUboot bootloader with swap, if only a single image is available,
	 * the "image_index" and "slot" properties may be omitted
	 */
	if (g_strcmp0(bootloader_name, "MCUBOOT") == 0 && files_cnt == 1) {
		*flash_area_id = 0;
		return TRUE;
	}

	if (!json_object_has_member(obj, "image_index")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "missing image_index property");
		return FALSE;
	}
	image_idx_str = json_object_get_string_member(obj, "image_index");
	if (image_idx_str == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "missing image_index property");
		return FALSE;
	}
	if (!fu_strtoll(image_idx_str,
			&image_idx,
			G_MININT64,
			G_MAXINT64,
			FU_INTEGER_BASE_AUTO,
			error)) {
		g_prefix_error(error, "fu_strtoll failed for image_index:");
		return FALSE;
	}

	if (!json_object_has_member(obj, "slot")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "missing slot property");
		return FALSE;
	}
	slot_str = json_object_get_string_member(obj, "slot");
	if (slot_str == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "missing slot property");
		return FALSE;
	}
	if (!fu_strtoll(slot_str, &slot, G_MININT64, G_MAXINT64, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error(error, "fu_strtoll failed for slot:");
		return FALSE;
	}

	if (image_idx != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "unsupported image_index property");
		return FALSE;
	}
	if (slot != 0 && slot != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "unsupported slot property");
		return FALSE;
	}

	*flash_area_id = slot;
	return TRUE;
}

static gboolean
fu_nordic_hid_archive_parse_file_get_flash_area_id(JsonObject *obj,
						   guint *flash_area_id,
						   gint64 manifest_ver,
						   guint file_idx,
						   const gchar *bootloader_name,
						   guint files_cnt,
						   GError **error)
{
	/* for manifest version 0, the images are expected to be listed in strict order */
	if (manifest_ver == 0) {
		*flash_area_id = file_idx;
		return TRUE;
	}

	if (manifest_ver == 1) {
		return fu_nordic_hid_archive_parse_file_get_flash_area_id_v1(obj,
									     flash_area_id,
									     bootloader_name,
									     files_cnt,
									     error);
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unsupported manifest version");
	return FALSE;
}

static gboolean
fu_nordic_hid_archive_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    FwupdInstallFlags flags,
			    GError **error)
{
	JsonNode *json_root_node;
	JsonObject *json_obj;
	JsonArray *json_files;
	gint64 manifest_ver;
	guint files_cnt = 0;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) manifest = NULL;
	g_autoptr(JsonParser) parser = json_parser_new();

	/* load archive */
	archive = fu_archive_new_stream(stream, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
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
	if (manifest_ver < MIN_VERSION_FORMAT || manifest_ver > MAX_VERSION_FORMAT) {
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
		guint flash_area_id;
		guint image_addr = 0;
		JsonObject *obj = json_array_get_object_element(json_files, i);
		g_autoptr(FuFirmware) image = NULL;
		g_autofree gchar *board_name = NULL;
		g_autofree gchar *fwupd_image_id = NULL;
		g_autoptr(GBytes) blob = NULL;

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

		bootloader_name = fu_nordic_hid_archive_parse_file_get_bootloader_name(obj, error);
		if (bootloader_name == NULL)
			return FALSE;

		image = fu_nordic_hid_archive_parse_file_image_create(bootloader_name, error);
		if (image == NULL)
			return FALSE;

		board_name =
		    fu_nordic_hid_archive_parse_file_get_board_name(obj, manifest_ver, error);
		if (board_name == NULL)
			return FALSE;

		if (!fu_nordic_hid_archive_parse_file_get_flash_area_id(obj,
									&flash_area_id,
									manifest_ver,
									i,
									bootloader_name,
									files_cnt,
									error))
			return FALSE;

		/* used image ID format: <board>_<bl>_<bank>N, i.e "nrf52840dk_B0_bank0" */
		fwupd_image_id =
		    g_strdup_printf("%s_%s_bank%01u", board_name, bootloader_name, flash_area_id);

		if (!fu_firmware_parse(image, blob, flags | FWUPD_INSTALL_FLAG_NO_SEARCH, error))
			return FALSE;

		fu_firmware_set_id(image, fwupd_image_id);
		fu_firmware_set_idx(image, i);

		if (json_object_has_member(obj, "load_address")) {
			image_addr = json_object_get_int_member(obj, "load_address");
			fu_firmware_set_addr(image, image_addr);
		}

		if (!fu_firmware_add_image_full(firmware, image, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_nordic_hid_archive_init(FuNordicHidArchive *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), 1024);
}

static void
fu_nordic_hid_archive_class_init(FuNordicHidArchiveClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_nordic_hid_archive_parse;
}
