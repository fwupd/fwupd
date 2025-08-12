/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-rdfu-firmware.h"
#include "json-glib/json-glib.h"

#define FU_LOGITECH_RDFU_FIRMWARE_VERSION 1
#define FU_LOGITECH_RDFU_MAGIC_ASCII_SIZE 22 /* 0x + 10 hex */

struct _FuLogitechRdfuFirmware {
	FuFirmware parent_instance;
	gchar *payload_name;
	gchar *model_id;
	GByteArray *magic;
	GPtrArray *blocks; /* GByteArray */
};

G_DEFINE_TYPE(FuLogitechRdfuFirmware, fu_logitech_rdfu_firmware, FU_TYPE_FIRMWARE)

gchar *
fu_logitech_rdfu_firmware_get_model_id(FuLogitechRdfuFirmware *self, GError **error)
{
	return g_strdup(self->model_id);
}

GByteArray *
fu_logitech_rdfu_firmware_get_magic(FuLogitechRdfuFirmware *self, GError **error)
{
	return g_byte_array_ref(self->magic);
}

GPtrArray *
fu_logitech_rdfu_firmware_get_blocks(FuLogitechRdfuFirmware *self, GError **error)
{
	return g_ptr_array_ref(self->blocks);
}

static gboolean
fu_logitech_rdfu_firmware_block_add(FuLogitechRdfuFirmware *self, JsonNode *node, GError **error)
{
	JsonObject *json_obj;
	const gchar *block_str;
	g_autoptr(GByteArray) block = NULL;
	g_autoptr(GError) error_local = NULL;

	if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "RDFU firmware contains incorrect payloads node");
		return FALSE;
	}

	json_obj = json_node_get_object(node);
	if (!json_object_has_member(json_obj, "data")) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "payload %s has no data key",
			    self->payload_name);
		return FALSE;
	}

	block_str = json_object_get_string_member_with_default(json_obj, "data", NULL);
	if (block_str == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "payload %s has no data",
			    self->payload_name);
		return FALSE;
	}

	if (strlen(block_str) % 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "payload %s has incorrect size %u",
			    self->payload_name,
			    (guint)strlen(block_str));
		return FALSE;
	}

	block = fu_byte_array_from_string(block_str, &error_local);
	if (block == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unable to serialize payload %s",
			    self->payload_name);
		return FALSE;
	}

	g_ptr_array_add(self->blocks, g_steal_pointer(&block));
	return TRUE;
}

static gboolean
fu_logitech_rdfu_firmware_entry_add(FuFirmware *firmware, JsonNode *node, GError **error)

{
	FuLogitechRdfuFirmware *self = FU_LOGITECH_RDFU_FIRMWARE(firmware);
	JsonObject *json_obj;
	guint str_offset = 0;
	guint64 entity;
	guint64 revision;
	guint64 build;
	const gchar *entity_str;
	const gchar *magic_str;
	const gchar *payload_str;
	const gchar *model_id_str;
	const gchar *name_str;
	const gchar *revision_str;
	const gchar *build_str;
	g_autofree gchar *version = NULL;
	g_autofree gchar *tmp = NULL;
	g_autoptr(GByteArray) model_id = NULL;

	if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "incorrect contents node");
		return FALSE;
	}

	json_obj = json_node_get_object(node);
	if (!json_object_has_member(json_obj, "entity")) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "has no entity");
		return FALSE;
	}
	entity_str = json_object_get_string_member_with_default(json_obj, "entity", NULL);
	if (!fu_strtoull(entity_str, &entity, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
		return FALSE;

	fu_firmware_set_id(FU_FIRMWARE(self), entity_str);

	magic_str = json_object_get_string_member_with_default(json_obj, "magicStr", NULL);
	if (magic_str == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "has no magic");
		return FALSE;
	}
	if (strlen(magic_str) != FU_LOGITECH_RDFU_MAGIC_ASCII_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "has incorrect magic");
		return FALSE;
	}

	payload_str = json_object_get_string_member_with_default(json_obj, "payload", NULL);
	if (payload_str == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "has no payload");
		return FALSE;
	}
	g_debug("RDFU firmware for entity %u payload = %s", (guint)entity, payload_str);

	if (!json_object_has_member(json_obj, "modelId")) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "has no modelId");
		return FALSE;
	}
	model_id_str = json_object_get_string_member_with_default(json_obj, "modelId", NULL);
	/* just to validate if modelId is in a hexadecimal string format */
	if (g_str_has_prefix(model_id_str, "0x"))
		str_offset = 2;
	model_id = fu_byte_array_from_string(model_id_str + str_offset, error);
	if (model_id == NULL)
		return FALSE;

	if (!json_object_has_member(json_obj, "name")) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "has no name");
		return FALSE;
	}
	name_str = json_object_get_string_member_with_default(json_obj, "name", NULL);

	if (!json_object_has_member(json_obj, "revision")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "has no revision");
		return FALSE;
	}
	revision_str = json_object_get_string_member_with_default(json_obj, "revision", NULL);
	if (!fu_strtoull(revision_str, &revision, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
		return FALSE;

	if (!json_object_has_member(json_obj, "build")) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "has no build");
		return FALSE;
	}
	build_str = json_object_get_string_member_with_default(json_obj, "build", NULL);
	/* should be in BCD format already but let's be tolerant to absent leading 0 */
	if (!fu_strtoull(build_str, &build, 0, G_MAXUINT16, FU_INTEGER_BASE_16, error)) {
		return FALSE;
	}

	/* skip "0x" prefix */
	self->magic = fu_byte_array_from_string(magic_str + 2, error);
	if (self->magic == NULL)
		return FALSE;
	/* model id should be in same format as returned for device by getFwInfo */
	tmp = fu_byte_array_to_string(model_id);
	self->model_id = g_ascii_strup(tmp, -1);
	self->payload_name = g_strdup(payload_str);
	self->blocks = g_ptr_array_new_with_free_func((GDestroyNotify)g_byte_array_unref);
	version = g_strdup_printf("%s.%02x_B%04x", name_str, (guint)revision, (guint)build);
	fu_firmware_set_version(firmware, version);

	return TRUE;
}

static gboolean
fu_logitech_rdfu_firmware_compare_payload(gconstpointer item, gconstpointer payload)
{
	FuLogitechRdfuFirmware *entity_fw = (FuLogitechRdfuFirmware *)item;

	if (g_strcmp0(entity_fw->payload_name, payload) != 0)
		return FALSE;

	return TRUE;
}

static gboolean
fu_logitech_rdfu_firmware_parse(FuFirmware *firmware,
				GInputStream *stream,
				FuFirmwareParseFlags flags,
				GError **error)
{
	JsonNode *json_root_node;
	JsonObject *json_obj;
	JsonObject *json_payloads_obj;
	JsonArray *contents;
	const gchar *firmware_str;
	guint64 firmware_ver;
	gsize streamsz;
	g_autoptr(JsonParser) parser = json_parser_new();
	g_autoptr(GList) payloads_list = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, error)) {
		g_prefix_error_literal(error, "seek to start: ");
		return FALSE;
	}

	if (!json_parser_load_from_stream(parser, stream, NULL, error)) {
		g_prefix_error_literal(error, "failed to parse RDFU firmware: ");
		return FALSE;
	}
	json_root_node = json_parser_get_root(parser);
	if (json_root_node == NULL || !JSON_NODE_HOLDS_OBJECT(json_root_node)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "RDFU firmware has no root");
		return FALSE;
	}
	json_obj = json_node_get_object(json_root_node);
	if (!json_object_has_member(json_obj, "fileVersion")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "RDFU firmware has invalid format");
		return FALSE;
	}
	firmware_str = json_object_get_string_member_with_default(json_obj, "fileVersion", "0");
	if (!fu_strtoull(firmware_str,
			 &firmware_ver,
			 1,
			 FU_LOGITECH_RDFU_FIRMWARE_VERSION,
			 FU_INTEGER_BASE_AUTO,
			 error))
		return FALSE;

	if (!json_object_has_member(json_obj, "contents")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "unable to find contents");
		return FALSE;
	}
	contents = json_object_get_array_member(json_obj, "contents");
	if (json_array_get_length(contents) < 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "empty contents array");
		return FALSE;
	}
	/* adding blocks to the entity FW */
	for (guint i = 0; i < json_array_get_length(contents); i++) {
		JsonNode *node = json_array_get_element(contents, i);
		g_autoptr(FuFirmware) entity_fw =
		    FU_FIRMWARE(g_object_new(FU_TYPE_LOGITECH_RDFU_FIRMWARE, NULL));
		if (!fu_logitech_rdfu_firmware_entry_add(entity_fw, node, error)) {
			g_prefix_error(error, "RDFU firmware contents[%u]: ", i);
			return FALSE;
		}
		fu_firmware_add_image(firmware, entity_fw);
	}

	if (!json_object_has_member(json_obj, "payloads")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "unable to find payloads");
		return FALSE;
	}

	json_payloads_obj = json_object_get_object_member(json_obj, "payloads");
	payloads_list = json_object_get_members(json_payloads_obj);

	for (GList *l = payloads_list; l != NULL; l = g_list_next(l)) {
		guint index = 0;
		JsonArray *blocks;
		FuLogitechRdfuFirmware *entity_fw = NULL;
		g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);

		if (!g_ptr_array_find_with_equal_func(images,
						      l->data,
						      fu_logitech_rdfu_firmware_compare_payload,
						      &index))
			continue;

		entity_fw = (FuLogitechRdfuFirmware *)images->pdata[index];
		g_debug("found payload %s for entity %s",
			(gchar *)l->data,
			fu_firmware_get_id(FU_FIRMWARE(entity_fw)));

		json_obj = json_object_get_object_member(json_payloads_obj, l->data);

		if (!json_object_has_member(json_obj, "blocks")) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "payload %s has no blocks",
				    (gchar *)l->data);
			return FALSE;
		}
		blocks = json_object_get_array_member(json_obj, "blocks");
		if (json_array_get_length(contents) < 1) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "empty blocks for payload %s",
				    (gchar *)l->data);
			return FALSE;
		}
		/* adding blocks to the entity FW */
		for (guint i = 0; i < json_array_get_length(blocks); i++) {
			JsonNode *block = json_array_get_element(blocks, i);
			if (!fu_logitech_rdfu_firmware_block_add(entity_fw, block, error)) {
				g_prefix_error(error, "unable to parse block %u: ", i);
				return FALSE;
			}
		}
		g_debug("added payload %s with %u blocks",
			entity_fw->payload_name,
			entity_fw->blocks->len);
	}

	/* success */
	return TRUE;
}

static void
fu_logitech_rdfu_firmware_export(FuFirmware *firmware,
				 FuFirmwareExportFlags flags,
				 XbBuilderNode *bn)
{
	FuLogitechRdfuFirmware *self = FU_LOGITECH_RDFU_FIRMWARE(firmware);

	if (self->model_id != NULL)
		fu_xmlb_builder_insert_kv(bn, "modelId", self->model_id);
	if (self->payload_name != NULL)
		fu_xmlb_builder_insert_kv(bn, "payload", self->payload_name);
	if (self->magic != NULL) {
		g_autofree gchar *magic = fu_byte_array_to_string(self->magic);
		fu_xmlb_builder_insert_kv(bn, "magic", magic);
	}
	if (self->blocks != NULL)
		fu_xmlb_builder_insert_kx(bn, "blocks", self->blocks->len);
}

static void
fu_logitech_rdfu_firmware_finalize(GObject *object)
{
	FuLogitechRdfuFirmware *self = FU_LOGITECH_RDFU_FIRMWARE(object);

	g_free(self->model_id);
	g_free(self->payload_name);
	if (self->magic != NULL)
		g_byte_array_unref(self->magic);
	if (self->blocks != NULL)
		g_ptr_array_unref(self->blocks);

	G_OBJECT_CLASS(fu_logitech_rdfu_firmware_parent_class)->finalize(object);
}

static void
fu_logitech_rdfu_firmware_init(FuLogitechRdfuFirmware *self)
{
}

static void
fu_logitech_rdfu_firmware_class_init(FuLogitechRdfuFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_logitech_rdfu_firmware_finalize;
	firmware_class->parse = fu_logitech_rdfu_firmware_parse;
	firmware_class->export = fu_logitech_rdfu_firmware_export;
}
