/*
 * Copyright 2024 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-rdfu-entity.h"

#define FU_LOGITECH_RDFU_MAGIC_ASCII_SIZE 22 /* 0x + 10 hex */

struct _FuLogitechRdfuEntity {
	FuFirmware parent_instance;
	gchar *model_id;
	GByteArray *magic;
	GPtrArray *blocks; /* GByteArray */
};

G_DEFINE_TYPE(FuLogitechRdfuEntity, fu_logitech_rdfu_entity, FU_TYPE_FIRMWARE)

const gchar *
fu_logitech_rdfu_entity_get_model_id(FuLogitechRdfuEntity *self)
{
	return self->model_id;
}

GByteArray *
fu_logitech_rdfu_entity_get_magic(FuLogitechRdfuEntity *self)
{
	return self->magic;
}

GPtrArray *
fu_logitech_rdfu_entity_get_blocks(FuLogitechRdfuEntity *self)
{
	return self->blocks;
}

gboolean
fu_logitech_rdfu_entity_add_block(FuLogitechRdfuEntity *self,
				  FwupdJsonObject *json_obj,
				  GError **error)
{
	const gchar *block_str;
	g_autoptr(GByteArray) block = NULL;

	block_str = fwupd_json_object_get_string(json_obj, "data", error);
	if (block_str == NULL)
		return FALSE;
	block = fu_byte_array_from_string(block_str, error);
	if (block == NULL)
		return FALSE;

	/* success */
	g_ptr_array_add(self->blocks, g_steal_pointer(&block));
	return TRUE;
}

gboolean
fu_logitech_rdfu_entity_add_entry(FuLogitechRdfuEntity *self,
				  FwupdJsonObject *json_obj,
				  GError **error)
{
	guint str_offset = 0;
	guint64 entity = 0;
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

	/* yes, this is encoded as a string */
	entity_str = fwupd_json_object_get_string(json_obj, "entity", error);
	if (entity_str == NULL)
		return FALSE;
	if (!fu_strtoull(entity_str, &entity, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
		return FALSE;
	fu_firmware_set_idx(FU_FIRMWARE(self), entity);

	magic_str = fwupd_json_object_get_string(json_obj, "magicStr", NULL);
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

	payload_str = fwupd_json_object_get_string(json_obj, "payload", error);
	if (payload_str == NULL)
		return FALSE;
	fu_firmware_set_id(FU_FIRMWARE(self), payload_str);

	model_id_str = fwupd_json_object_get_string(json_obj, "modelId", error);
	if (model_id_str == NULL)
		return FALSE;
	/* just to validate if modelId is in a hexadecimal string format */
	if (g_str_has_prefix(model_id_str, "0x"))
		str_offset = 2;
	model_id = fu_byte_array_from_string(model_id_str + str_offset, error);
	if (model_id == NULL)
		return FALSE;

	name_str = fwupd_json_object_get_string(json_obj, "name", error);
	if (name_str == NULL)
		return FALSE;

	revision_str = fwupd_json_object_get_string(json_obj, "revision", error);
	if (revision_str == NULL)
		return FALSE;
	if (!fu_strtoull(revision_str, &revision, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
		return FALSE;

	build_str = fwupd_json_object_get_string(json_obj, "build", error);
	if (build_str == NULL)
		return FALSE;
	/* should be in BCD format already but let's be tolerant to absent leading 0 */
	if (!fu_strtoull(build_str, &build, 0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
		return FALSE;

	/* skip "0x" prefix */
	self->magic = fu_byte_array_from_string(magic_str + 2, error);
	if (self->magic == NULL)
		return FALSE;

	/* model id should be in same format as returned for device by getFwInfo */
	tmp = fu_byte_array_to_string(model_id);
	self->model_id = g_ascii_strup(tmp, -1);
	version = g_strdup_printf("%s.%02x_B%04x", name_str, (guint)revision, (guint)build);
	fu_firmware_set_version(FU_FIRMWARE(self), version);

	/* success */
	return TRUE;
}

static void
fu_logitech_rdfu_entity_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuLogitechRdfuEntity *self = FU_LOGITECH_RDFU_ENTITY(firmware);

	if (self->model_id != NULL)
		fu_xmlb_builder_insert_kv(bn, "modelId", self->model_id);
	if (self->magic != NULL) {
		g_autofree gchar *magic = fu_byte_array_to_string(self->magic);
		fu_xmlb_builder_insert_kv(bn, "magic", magic);
	}
	if (self->blocks != NULL)
		fu_xmlb_builder_insert_kx(bn, "blocks", self->blocks->len);
}

static void
fu_logitech_rdfu_entity_finalize(GObject *object)
{
	FuLogitechRdfuEntity *self = FU_LOGITECH_RDFU_ENTITY(object);

	g_free(self->model_id);
	if (self->magic != NULL)
		g_byte_array_unref(self->magic);
	g_ptr_array_unref(self->blocks);

	G_OBJECT_CLASS(fu_logitech_rdfu_entity_parent_class)->finalize(object);
}

static void
fu_logitech_rdfu_entity_init(FuLogitechRdfuEntity *self)
{
	self->blocks = g_ptr_array_new_with_free_func((GDestroyNotify)g_byte_array_unref);
}

static void
fu_logitech_rdfu_entity_class_init(FuLogitechRdfuEntityClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_logitech_rdfu_entity_finalize;
	firmware_class->export = fu_logitech_rdfu_entity_export;
}

FuLogitechRdfuEntity *
fu_logitech_rdfu_entity_new(void)
{
	return g_object_new(FU_TYPE_LOGITECH_RDFU_ENTITY, NULL);
}
