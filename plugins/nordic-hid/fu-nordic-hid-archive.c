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
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuNordicHidArchive, fu_nordic_hid_archive, FU_TYPE_FIRMWARE)

static const gchar *
fu_nordic_hid_archive_parse_file_get_bootloader_name(FwupdJsonObject *json_obj, GError **error)
{
	if (fwupd_json_object_has_node(json_obj, "version_B0"))
		return "B0";
	if (fwupd_json_object_has_node(json_obj, "version_MCUBOOT"))
		return "MCUBOOT";
	if (fwupd_json_object_has_node(json_obj, "version_MCUBOOT+XIP"))
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
fu_nordic_hid_archive_parse_file_get_board_name(FwupdJsonObject *json_obj,
						gint64 manifest_ver,
						GError **error)
{
	g_auto(GStrv) board_split = NULL;
	const gchar *board_name_readout = NULL;

	board_name_readout = fwupd_json_object_get_string(json_obj, "board", error);
	if (board_name_readout == NULL) {
		g_prefix_error_literal(error, "manifest invalid as has no target information: ");
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
fu_nordic_hid_archive_parse_file_get_flash_area_id_v1(FwupdJsonObject *json_obj,
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

	image_idx_str = fwupd_json_object_get_string(json_obj, "image_index", error);
	if (image_idx_str == NULL) {
		g_prefix_error_literal(error, "missing property: ");
		return FALSE;
	}
	if (!fu_strtoll(image_idx_str,
			&image_idx,
			G_MININT64,
			G_MAXINT64,
			FU_INTEGER_BASE_AUTO,
			error)) {
		g_prefix_error_literal(error, "failed to parse image_index: ");
		return FALSE;
	}

	slot_str = fwupd_json_object_get_string(json_obj, "slot", error);
	if (slot_str == NULL)
		return FALSE;
	if (!fu_strtoll(slot_str, &slot, G_MININT64, G_MAXINT64, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error_literal(error, "failed to parse slot: ");
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
fu_nordic_hid_archive_parse_file_get_flash_area_id(FwupdJsonObject *obj,
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
			    FuFirmwareParseFlags flags,
			    GError **error)
{
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FwupdJsonArray) json_files = NULL;
	gint64 manifest_ver = 0;
	guint files_cnt = 0;
	g_autoptr(FuFirmware) archive = fu_zip_firmware_new();
	g_autoptr(GBytes) manifest = NULL;
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();

	/* set appropriate limits */
	fwupd_json_parser_set_max_depth(json_parser, 10);
	fwupd_json_parser_set_max_items(json_parser, 100);
	fwupd_json_parser_set_max_quoted(json_parser, 10000);

	/* load archive */
	if (!fu_firmware_parse_stream(archive,
				      stream,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_ONLY_BASENAME,
				      error))
		return FALSE;
	manifest = fu_firmware_get_image_by_id_bytes(archive, "manifest.json", error);
	if (manifest == NULL)
		return FALSE;

	/* parse JSON */
	json_node = fwupd_json_parser_load_from_bytes(json_parser,
						      manifest,
						      FWUPD_JSON_LOAD_FLAG_NONE,
						      error);
	if (json_node == NULL)
		return FALSE;
	json_obj = fwupd_json_node_get_object(json_node, error);
	if (json_obj == NULL)
		return FALSE;

	if (!fwupd_json_object_get_integer(json_obj, "format-version", &manifest_ver, error))
		return FALSE;
	if (manifest_ver < MIN_VERSION_FORMAT || manifest_ver > MAX_VERSION_FORMAT) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "unsupported manifest version");
		return FALSE;
	}

	json_files = fwupd_json_object_get_array(json_obj, "files", error);
	if (json_files == NULL)
		return FALSE;
	files_cnt = fwupd_json_array_get_size(json_files);
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
		gint64 image_addr = 0;
		g_autoptr(FwupdJsonObject) json_obj_tmp = NULL;
		g_autoptr(FuFirmware) image = NULL;
		g_autofree gchar *board_name = NULL;
		g_autofree gchar *fwupd_image_id = NULL;
		g_autoptr(GBytes) blob = NULL;

		json_obj_tmp = fwupd_json_array_get_object(json_files, i, error);
		if (json_obj_tmp == NULL)
			return FALSE;
		filename = fwupd_json_object_get_string(json_obj_tmp, "file", error);
		if (filename == NULL) {
			g_prefix_error_literal(error, "manifest invalid: ");
			return FALSE;
		}
		blob = fu_firmware_get_image_by_id_bytes(archive, filename, error);
		if (blob == NULL)
			return FALSE;

		bootloader_name =
		    fu_nordic_hid_archive_parse_file_get_bootloader_name(json_obj_tmp, error);
		if (bootloader_name == NULL)
			return FALSE;

		image = fu_nordic_hid_archive_parse_file_image_create(bootloader_name, error);
		if (image == NULL)
			return FALSE;

		board_name = fu_nordic_hid_archive_parse_file_get_board_name(json_obj_tmp,
									     manifest_ver,
									     error);
		if (board_name == NULL)
			return FALSE;

		if (!fu_nordic_hid_archive_parse_file_get_flash_area_id(json_obj_tmp,
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

		if (!fu_firmware_parse_bytes(image,
					     blob,
					     0x0,
					     flags | FU_FIRMWARE_PARSE_FLAG_NO_SEARCH,
					     error))
			return FALSE;

		fu_firmware_set_id(image, fwupd_image_id);
		fu_firmware_set_idx(image, i);

		if (!fwupd_json_object_get_integer_with_default(json_obj_tmp,
								"load_address",
								&image_addr,
								-1,
								error))
			return FALSE;
		if (image_addr != -1)
			fu_firmware_set_addr(image, image_addr);

		if (!fu_firmware_add_image(firmware, image, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_nordic_hid_archive_init(FuNordicHidArchive *self)
{
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_NORDIC_HID_FIRMWARE_B0);
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_NORDIC_HID_FIRMWARE_MCUBOOT);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 1024);
}

static void
fu_nordic_hid_archive_class_init(FuNordicHidArchiveClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_nordic_hid_archive_parse;
}
