/*
 * Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-telink-dfu-archive.h"
#include "fu-telink-dfu-common.h"
#include "fu-telink-dfu-firmware.h"

#if DEBUG_ARCHIVE == 1
static gboolean
iter_archive_callback(FuArchive *archive,
		      const gchar *filename,
		      GBytes *bytes,
		      gpointer user_data,
		      GError **error)
{
	LOGD("found %s", filename);
	return TRUE;
}
#endif

struct _FuTelinkDfuArchive {
	FuFirmware parent_instance;
};

typedef struct {
	guint32 version_raw;
	gchar *version;
} FuTelinkDfuArchivePrivate;

// G_DEFINE_TYPE(FuTelinkDfuArchive, fu_telink_dfu_archive, FU_TYPE_FIRMWARE)
G_DEFINE_TYPE_WITH_PRIVATE(FuTelinkDfuArchive, fu_telink_dfu_archive, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_telink_dfu_archive_get_instance_private(o))

#define JSON_FORMAT_VERSION_MAX 0

static gboolean
fu_telink_dfu_archive_load_file(FuTelinkDfuArchive *self,
				FuArchive *archive,
				JsonObject *obj,
				guint i,
				FwupdInstallFlags flags,
				GError **error)
{
	const gchar *bootloader_name = NULL;
	const gchar *filename = NULL;
	const gchar *board_name = NULL;
	g_autofree gchar *image_id = NULL;
	g_auto(GStrv) splits = NULL;
	g_auto(GStrv) fw_ver = NULL;
	g_autoptr(FuFirmware) image = NULL;
	g_autoptr(GBytes) blob = NULL;
	FuTelinkDfuArchivePrivate *priv = GET_PRIVATE(self);
	guint idx;
	guint64 tmp_u64;
	const gchar *bl_type_keys[] = {"beta", "ota-v1"};
	gboolean res;

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

	res = FALSE;
	if (!json_object_has_member(obj, "bootloader_type")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "missing param: bootloader_type");
		return FALSE;
	}
	bootloader_name = json_object_get_string_member(obj, "bootloader_type");
	for (idx = 0; idx < sizeof(bl_type_keys) / sizeof(bl_type_keys[0]); idx++) {
		if (g_strcmp0(bootloader_name, bl_type_keys[idx])) {
			res = TRUE;
		}
	}
	if (res == FALSE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "bad param: bootloader_type");
		return FALSE;
	}
	image = fu_telink_dfu_firmware_new();

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
	if (!fu_firmware_parse(image, blob, flags, error))
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
	splits = g_strsplit(json_object_get_string_member(obj, "image_version"), ".", 3);
	if (g_strv_length(splits) < 3) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "bad param: image_version");
		return FALSE;
	}

	priv->version_raw = 0;
	fu_strtoull(splits[0], &tmp_u64, 0, G_MAXUINT32, error);
	priv->version_raw |= (guint32)(tmp_u64 << 24);
	fu_strtoull(splits[1], &tmp_u64, 0, G_MAXUINT32, error);
	priv->version_raw |= (guint32)(tmp_u64 << 16);
	fu_strtoull(splits[2], &tmp_u64, 0, G_MAXUINT32, error);
	priv->version_raw |= (guint32)tmp_u64;
	priv->version = fu_version_from_uint32(priv->version_raw, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_firmware_set_version_raw(FU_FIRMWARE(self), priv->version_raw);
	fu_firmware_set_version(FU_FIRMWARE(self), priv->version);
	LOGD("version_raw=0x%x", priv->version_raw);
	LOGD("version=%s", priv->version);

	/* success */
	return TRUE;
}

static gboolean
fu_telink_dfu_archive_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
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
#if DEBUG_ARCHIVE == 1
	gboolean ret;
#endif

	// 1. load archive
	archive = fu_archive_new_stream(stream, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;

		// 2. parse manifest.json
#if DEBUG_ARCHIVE == 1
	ret = fu_archive_iterate(archive, iter_archive_callback, NULL, error);
	if (!ret) {
		// todo
	}
#endif

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
	if (manifest_ver > JSON_FORMAT_VERSION_MAX) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "unsupported manifest version");
		return FALSE;
	}
	LOGD("manifest_ver=0x%u", manifest_ver);

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

FuFirmware *
fu_telink_dfu_archive_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_TELINK_DFU_ARCHIVE, NULL));
}
