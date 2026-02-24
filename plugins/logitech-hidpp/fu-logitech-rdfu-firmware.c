/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-rdfu-entity.h"
#include "fu-logitech-rdfu-firmware.h"

#define FU_LOGITECH_RDFU_FIRMWARE_VERSION 1

struct _FuLogitechRdfuFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuLogitechRdfuFirmware, fu_logitech_rdfu_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_logitech_rdfu_firmware_parse(FuFirmware *firmware,
				GInputStream *stream,
				FuFirmwareParseFlags flags,
				GError **error)
{
	const gchar *firmware_str;
	guint64 firmware_ver;
	gsize streamsz;
	g_autoptr(FwupdJsonArray) contents = NULL;
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FwupdJsonObject) json_obj_payloads = NULL;
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();
	g_autoptr(GPtrArray) keys = NULL;

	/* set appropriate limits */
	fwupd_json_parser_set_max_depth(json_parser, 10);
	fwupd_json_parser_set_max_items(json_parser, 100);
	fwupd_json_parser_set_max_quoted(json_parser, 50000);

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, error)) {
		g_prefix_error_literal(error, "seek to start: ");
		return FALSE;
	}

	json_node = fwupd_json_parser_load_from_stream(json_parser,
						       stream,
						       FWUPD_JSON_LOAD_FLAG_NONE,
						       error);
	if (json_node == NULL)
		return FALSE;
	json_obj = fwupd_json_node_get_object(json_node, error);
	if (json_obj == NULL)
		return FALSE;
	firmware_str = fwupd_json_object_get_string(json_obj, "fileVersion", error);
	if (firmware_str == NULL)
		return FALSE;
	if (!fu_strtoull(firmware_str,
			 &firmware_ver,
			 1,
			 FU_LOGITECH_RDFU_FIRMWARE_VERSION,
			 FU_INTEGER_BASE_AUTO,
			 error))
		return FALSE;

	contents = fwupd_json_object_get_array(json_obj, "contents", error);
	if (contents == NULL)
		return FALSE;
	if (fwupd_json_array_get_size(contents) < 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "empty contents array");
		return FALSE;
	}

	/* adding blocks to the entity FW */
	for (guint i = 0; i < fwupd_json_array_get_size(contents); i++) {
		g_autoptr(FwupdJsonObject) json_obj_tmp = NULL;
		g_autoptr(FuLogitechRdfuEntity) entity_fw = fu_logitech_rdfu_entity_new();
		json_obj_tmp = fwupd_json_array_get_object(contents, i, error);
		if (json_obj_tmp == NULL)
			return FALSE;
		if (!fu_logitech_rdfu_entity_add_entry(entity_fw, json_obj_tmp, error)) {
			g_prefix_error(error, "RDFU firmware contents[%u]: ", i);
			return FALSE;
		}
		if (!fu_firmware_add_image(firmware, FU_FIRMWARE(entity_fw), error))
			return FALSE;
	}

	/* add payloads */
	json_obj_payloads = fwupd_json_object_get_object(json_obj, "payloads", error);
	if (json_obj_payloads == NULL)
		return FALSE;
	keys = fwupd_json_object_get_keys(json_obj_payloads);
	for (guint j = 0; j < keys->len; j++) {
		const gchar *payload_id = g_ptr_array_index(keys, j);
		g_autoptr(FuFirmware) entity_fw = NULL;
		g_autoptr(FwupdJsonArray) json_arr_blocks = NULL;
		g_autoptr(FwupdJsonObject) json_obj_payload = NULL;

		/* get entity child */
		entity_fw = fu_firmware_get_image_by_id(firmware, payload_id, error);
		if (entity_fw == NULL)
			return FALSE;
		json_obj_payload =
		    fwupd_json_object_get_object(json_obj_payloads, payload_id, error);
		if (json_obj_payload == NULL)
			return FALSE;
		json_arr_blocks = fwupd_json_object_get_array(json_obj_payload, "blocks", error);
		if (json_arr_blocks == NULL) {
			g_prefix_error(error, "failed to parse payload %s: ", payload_id);
			return FALSE;
		}
		if (fwupd_json_array_get_size(json_arr_blocks) < 1) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "empty blocks for payload %s",
				    payload_id);
			return FALSE;
		}
		/* adding blocks to the entity FW */
		for (guint i = 0; i < fwupd_json_array_get_size(json_arr_blocks); i++) {
			g_autoptr(FwupdJsonObject) json_obj_block = NULL;

			json_obj_block = fwupd_json_array_get_object(json_arr_blocks, i, error);
			if (json_obj_block == NULL)
				return FALSE;
			if (!fu_logitech_rdfu_entity_add_block(FU_LOGITECH_RDFU_ENTITY(entity_fw),
							       json_obj_block,
							       error)) {
				g_prefix_error(error, "unable to parse block %u: ", i);
				return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

static void
fu_logitech_rdfu_firmware_init(FuLogitechRdfuFirmware *self)
{
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_LOGITECH_RDFU_ENTITY);
}

static void
fu_logitech_rdfu_firmware_class_init(FuLogitechRdfuFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_logitech_rdfu_firmware_parse;
}
