/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEfiLoadOption"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-efi-device-path-list.h"
#include "fu-efi-file-path-device-path.h"
#include "fu-efi-hard-drive-device-path.h"
#include "fu-efi-load-option.h"
#include "fu-input-stream.h"
#include "fu-mem.h"
#include "fu-string.h"

struct _FuEfiLoadOption {
	FuFirmware parent_instance;
	guint32 attrs;
	FuEfiLoadOptionKind kind;
	GBytes *optional_data; /* only used when not a hive or path */
	GHashTable *metadata;  /* element-type: utf8:utf8 */
};

static void
fu_efi_load_option_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuEfiLoadOption,
		       fu_efi_load_option,
		       FU_TYPE_FIRMWARE,
		       0,
		       G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fu_efi_load_option_codec_iface_init))

#define FU_EFI_LOAD_OPTION_DESCRIPTION_SIZE_MAX 0x1000u /* bytes */

#define FU_EFI_LOAD_OPTION_HIVE_HEADER_VERSION_MIN 1

static void
fu_efi_load_option_set_optional_data(FuEfiLoadOption *self, GBytes *optional_data)
{
	g_return_if_fail(FU_IS_EFI_LOAD_OPTION(self));
	if (self->optional_data != NULL) {
		g_bytes_unref(self->optional_data);
		self->optional_data = NULL;
	}
	if (optional_data != NULL)
		self->optional_data = g_bytes_ref(optional_data);
}

/**
 * fu_efi_load_option_get_metadata:
 * @self: a #FuEfiLoadOption
 * @key: (not nullable): UTF-8 string
 * @error: (nullable): optional return location for an error
 *
 * Gets an optional attribute.
 *
 * Returns: UTF-8 string, or %NULL
 *
 * Since: 2.0.0
 **/
const gchar *
fu_efi_load_option_get_metadata(FuEfiLoadOption *self, const gchar *key, GError **error)
{
	const gchar *value;

	g_return_val_if_fail(FU_IS_EFI_LOAD_OPTION(self), NULL);
	g_return_val_if_fail(key != NULL, NULL);

	value = g_hash_table_lookup(self->metadata, key);
	if (value == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no attribute value for %s",
			    key);
		return NULL;
	}

	/* success */
	return value;
}

/**
 * fu_efi_load_option_get_kind:
 * @self: a #FuEfiLoadOption
 *
 * Gets the loadopt kind.
 *
 * Returns: a #FuEfiLoadOptionKind, e.g. %FU_EFI_LOAD_OPTION_KIND_HIVE
 *
 * Since: 2.0.6
 **/
FuEfiLoadOptionKind
fu_efi_load_option_get_kind(FuEfiLoadOption *self)
{
	g_return_val_if_fail(FU_IS_EFI_LOAD_OPTION(self), FU_EFI_LOAD_OPTION_KIND_UNKNOWN);
	return self->kind;
}

/**
 * fu_efi_load_option_set_kind:
 * @self: a #FuEfiLoadOption
 * @kind: a #FuEfiLoadOptionKind, e.g. %FU_EFI_LOAD_OPTION_KIND_HIVE
 *
 * Sets the loadopt kind.
 *
 * Since: 2.0.6
 **/
void
fu_efi_load_option_set_kind(FuEfiLoadOption *self, FuEfiLoadOptionKind kind)
{
	g_return_if_fail(FU_IS_EFI_LOAD_OPTION(self));
	g_return_if_fail(kind < FU_EFI_LOAD_OPTION_KIND_LAST);
	self->kind = kind;
}

/**
 * fu_efi_load_option_set_metadata:
 * @self: a #FuEfiLoadOption
 * @key: (not nullable): UTF-8 string
 * @value: (nullable): UTF-8 string, or %NULL
 *
 * Sets an optional attribute. If @value is %NULL then the key will be removed.
 *
 * NOTE: When the key is `Path`, any leading backslash will be stripped automatically and added
 * back as-required on export.
 *
 * Since: 2.0.0
 **/
void
fu_efi_load_option_set_metadata(FuEfiLoadOption *self, const gchar *key, const gchar *value)
{
	g_return_if_fail(FU_IS_EFI_LOAD_OPTION(self));
	g_return_if_fail(key != NULL);

	if (value == NULL) {
		g_hash_table_remove(self->metadata, key);
		return;
	}
	if (g_strcmp0(key, FU_EFI_LOAD_OPTION_METADATA_PATH) == 0 && value != NULL &&
	    g_str_has_prefix(value, "\\")) {
		value++;
	}
	g_hash_table_insert(self->metadata, g_strdup(key), g_strdup(value));
}

static gboolean
fu_efi_load_option_parse_optional_hive(FuEfiLoadOption *self,
				       GInputStream *stream,
				       gsize offset,
				       GError **error)
{
	g_autoptr(FuStructShimHive) st = NULL;
	guint8 items_count;

	st = fu_struct_shim_hive_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_shim_hive_get_header_version(st) <
	    FU_EFI_LOAD_OPTION_HIVE_HEADER_VERSION_MIN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "header version %u is not supported",
			    fu_struct_shim_hive_get_header_version(st));
		return FALSE;
	}
	offset += fu_struct_shim_hive_get_items_offset(st);

	/* items */
	items_count = fu_struct_shim_hive_get_items_count(st);
	for (guint i = 0; i < items_count; i++) {
		guint8 keysz;
		guint32 valuesz;
		g_autofree gchar *key = NULL;
		g_autofree gchar *value = NULL;
		g_autoptr(FuStructShimHiveItem) st_item = NULL;

		st_item = fu_struct_shim_hive_item_parse_stream(stream, offset, error);
		if (st_item == NULL)
			return FALSE;
		offset += st_item->len;

		/* key */
		keysz = fu_struct_shim_hive_item_get_key_length(st_item);
		if (keysz == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "zero key size is not supported");
			return FALSE;
		}
		key = fu_input_stream_read_string(stream, offset, keysz, error);
		if (key == NULL)
			return FALSE;
		offset += keysz;

		/* value */
		valuesz = fu_struct_shim_hive_item_get_value_length(st_item);
		if (valuesz > 0) {
			value = fu_input_stream_read_string(stream, offset, valuesz, error);
			if (value == NULL)
				return FALSE;
			offset += valuesz;
		}
		fu_efi_load_option_set_metadata(self, key, value != NULL ? value : "");
	}

	/* success */
	return TRUE;
}

static gboolean
fu_efi_load_option_parse_optional_path(FuEfiLoadOption *self, GBytes *opt_blob, GError **error)
{
	g_autofree gchar *optional_path = NULL;

	/* convert to UTF-8 */
	optional_path = fu_utf16_to_utf8_bytes(opt_blob, G_LITTLE_ENDIAN, error);
	if (optional_path == NULL)
		return FALSE;

	/* check is ASCII */
	if (optional_path[0] == '\0' || !g_str_is_ascii(optional_path)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "not ASCII data: %s",
			    optional_path);
		return FALSE;
	}
	fu_efi_load_option_set_metadata(self, FU_EFI_LOAD_OPTION_METADATA_PATH, optional_path);

	/* success */
	return TRUE;
}

static gboolean
fu_efi_load_option_parse_optional(FuEfiLoadOption *self,
				  GInputStream *stream,
				  gsize offset,
				  GError **error)
{
	gsize streamsz = 0;
	g_autoptr(GBytes) opt_blob = NULL;
	g_autoptr(GError) error_hive = NULL;
	g_autoptr(GError) error_path = NULL;

	/* try hive structure first */
	if (!fu_efi_load_option_parse_optional_hive(self, stream, offset, &error_hive)) {
		if (!g_error_matches(error_hive, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA)) {
			g_propagate_error(error, g_steal_pointer(&error_hive));
			return FALSE;
		}
		g_debug("not a shim hive, ignoring: %s", error_hive->message);
	} else {
		self->kind = FU_EFI_LOAD_OPTION_KIND_HIVE;
		return TRUE;
	}

	/* then UCS-2 path, and on ASCII failure just treat as a raw data blob */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	opt_blob = fu_input_stream_read_bytes(stream, offset, streamsz - offset, NULL, error);
	if (opt_blob == NULL)
		return FALSE;
	if (!fu_efi_load_option_parse_optional_path(self, opt_blob, &error_path)) {
		g_debug("not a path, saving as raw blob: %s", error_path->message);
		fu_efi_load_option_set_optional_data(self, opt_blob);
	} else {
		self->kind = FU_EFI_LOAD_OPTION_KIND_PATH;
		return TRUE;
	}

	/* success */
	self->kind = FU_EFI_LOAD_OPTION_KIND_DATA;
	return TRUE;
}

static gboolean
fu_efi_load_option_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuEfiLoadOption *self = FU_EFI_LOAD_OPTION(firmware);
	gsize offset = 0;
	gsize streamsz = 0;
	g_autofree gchar *id = NULL;
	g_autoptr(FuEfiDevicePathList) device_path_list = fu_efi_device_path_list_new();
	g_autoptr(GByteArray) buf_utf16 = g_byte_array_new();
	g_autoptr(GByteArray) st = NULL;

	/* parse header */
	st = fu_struct_efi_load_option_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;
	self->attrs = fu_struct_efi_load_option_get_attrs(st);
	offset += st->len;

	/* parse UTF-16 description */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	for (; offset < streamsz; offset += 2) {
		guint16 tmp = 0;
		if (buf_utf16->len > FU_EFI_LOAD_OPTION_DESCRIPTION_SIZE_MAX) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "description was too long, limit is 0x%x chars",
				    FU_EFI_LOAD_OPTION_DESCRIPTION_SIZE_MAX / 2);
			return FALSE;
		}
		if (!fu_input_stream_read_u16(stream, offset, &tmp, G_LITTLE_ENDIAN, error))
			return FALSE;
		if (tmp == 0)
			break;
		fu_byte_array_append_uint16(buf_utf16, tmp, G_LITTLE_ENDIAN);
	}
	id = fu_utf16_to_utf8_byte_array(buf_utf16, G_LITTLE_ENDIAN, error);
	if (id == NULL)
		return FALSE;
	fu_firmware_set_id(firmware, id);
	offset += 2;

	/* parse dp blob */
	if (!fu_firmware_parse_stream(FU_FIRMWARE(device_path_list), stream, offset, flags, error))
		return FALSE;
	if (!fu_firmware_add_image_full(firmware, FU_FIRMWARE(device_path_list), error))
		return FALSE;
	offset += fu_struct_efi_load_option_get_dp_size(st);

	/* optional data */
	if (offset < streamsz) {
		if (!fu_efi_load_option_parse_optional(self, stream, offset, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_efi_load_option_write_hive(FuEfiLoadOption *self, GError **error)
{
	GHashTableIter iter;
	guint items_count = g_hash_table_size(self->metadata);
	const gchar *key;
	const gchar *value;
	g_autoptr(FuStructShimHive) st = fu_struct_shim_hive_new();

	fu_struct_shim_hive_set_items_count(st, items_count);
	fu_struct_shim_hive_set_items_offset(st, FU_STRUCT_SHIM_HIVE_SIZE);
	g_hash_table_iter_init(&iter, self->metadata);
	while (g_hash_table_iter_next(&iter, (gpointer *)&key, (gpointer *)&value)) {
		guint32 keysz = strlen(key);
		g_autoptr(FuStructShimHiveItem) st_item = fu_struct_shim_hive_item_new();
		g_autoptr(GString) value_safe = g_string_new(value);

		/* required prefix for a path */
		if (g_strcmp0(key, FU_EFI_LOAD_OPTION_METADATA_PATH) == 0 && value_safe->len > 0 &&
		    !g_str_has_prefix(value_safe->str, "\\")) {
			g_string_prepend(value_safe, "\\");
		}
		fu_struct_shim_hive_item_set_key_length(st_item, keysz);
		fu_struct_shim_hive_item_set_value_length(st_item, value_safe->len);
		if (keysz > 0)
			g_byte_array_append(st_item, (const guint8 *)key, keysz);
		if (value_safe->len > 0) {
			g_byte_array_append(st_item,
					    (const guint8 *)value_safe->str,
					    value_safe->len);
		}

		/* add to hive */
		g_byte_array_append(st, st_item->data, st_item->len);
	}

	/* this covers all items, and so has to be done last */
	fu_struct_shim_hive_set_crc32(st, fu_crc32(FU_CRC_KIND_B32_STANDARD, st->data, st->len));

	/* success */
	return g_steal_pointer(&st);
}

static GByteArray *
fu_efi_load_option_write_path(FuEfiLoadOption *self, GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	const gchar *path = g_hash_table_lookup(self->metadata, FU_EFI_LOAD_OPTION_METADATA_PATH);
	g_autoptr(GString) str = g_string_new(path);

	/* is required if a path */
	if (!g_str_has_prefix(str->str, "\\"))
		g_string_prepend(str, "\\");
	buf = fu_utf8_to_utf16_byte_array(str->str,
					  G_LITTLE_ENDIAN,
					  FU_UTF_CONVERT_FLAG_APPEND_NUL,
					  error);
	if (buf == NULL)
		return NULL;
	return g_steal_pointer(&buf);
}

static GByteArray *
fu_efi_load_option_write(FuFirmware *firmware, GError **error)
{
	FuEfiLoadOption *self = FU_EFI_LOAD_OPTION(firmware);
	g_autoptr(GByteArray) buf_utf16 = NULL;
	g_autoptr(GByteArray) st = fu_struct_efi_load_option_new();
	g_autoptr(GBytes) dpbuf = NULL;

	/* header */
	fu_struct_efi_load_option_set_attrs(st, self->attrs);

	/* label */
	if (fu_firmware_get_id(firmware) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware ID required");
		return NULL;
	}
	buf_utf16 = fu_utf8_to_utf16_byte_array(fu_firmware_get_id(firmware),
						G_LITTLE_ENDIAN,
						FU_UTF_CONVERT_FLAG_APPEND_NUL,
						error);
	if (buf_utf16 == NULL)
		return NULL;
	g_byte_array_append(st, buf_utf16->data, buf_utf16->len);

	/* dpbuf */
	dpbuf = fu_firmware_get_image_by_gtype_bytes(firmware, FU_TYPE_EFI_DEVICE_PATH_LIST, error);
	if (dpbuf == NULL)
		return NULL;
	fu_struct_efi_load_option_set_dp_size(st, g_bytes_get_size(dpbuf));
	fu_byte_array_append_bytes(st, dpbuf);

	/* hive, path or data */
	if (self->kind == FU_EFI_LOAD_OPTION_KIND_HIVE) {
		g_autoptr(GByteArray) buf_hive = NULL;
		buf_hive = fu_efi_load_option_write_hive(self, error);
		if (buf_hive == NULL)
			return NULL;
		g_byte_array_append(st, buf_hive->data, buf_hive->len);
		fu_byte_array_align_up(st, FU_FIRMWARE_ALIGNMENT_512, 0x0); /* make atomic */
	} else if (self->kind == FU_EFI_LOAD_OPTION_KIND_PATH) {
		g_autoptr(GByteArray) buf_path = NULL;
		buf_path = fu_efi_load_option_write_path(self, error);
		if (buf_path == NULL)
			return NULL;
		g_byte_array_append(st, buf_path->data, buf_path->len);
	} else if (self->kind == FU_EFI_LOAD_OPTION_KIND_DATA && self->optional_data != NULL) {
		fu_byte_array_append_bytes(st, self->optional_data);
	}

	/* success */
	return g_steal_pointer(&st);
}

static gboolean
fu_efi_load_option_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEfiLoadOption *self = FU_EFI_LOAD_OPTION(firmware);
	const gchar *str;
	guint64 tmp;
	g_autoptr(GPtrArray) metadata = NULL;
	g_autoptr(XbNode) optional_data = NULL;

	/* simple properties */
	tmp = xb_node_query_text_as_uint(n, "attrs", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		self->attrs = tmp;

	/* simple properties */
	str = xb_node_query_text(n, "kind", NULL);
	if (str != NULL) {
		self->kind = fu_efi_load_option_kind_from_string(str);
		if (self->kind == FU_EFI_LOAD_OPTION_KIND_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid option kind type %s",
				    str);
			return FALSE;
		}
	}

	/* optional data */
	optional_data = xb_node_query_first(n, "optional_data", NULL);
	if (optional_data != NULL) {
		g_autoptr(GBytes) blob = NULL;
		if (xb_node_get_text(optional_data) != NULL) {
			gsize bufsz = 0;
			g_autofree guchar *buf = NULL;
			buf = g_base64_decode(xb_node_get_text(optional_data), &bufsz);
			blob = g_bytes_new(buf, bufsz);
		} else {
			blob = g_bytes_new(NULL, 0);
		}
		fu_efi_load_option_set_optional_data(self, blob);
		self->kind = FU_EFI_LOAD_OPTION_KIND_DATA;
	}
	metadata = xb_node_query(n, "metadata/*", 0, NULL);
	if (metadata != NULL) {
		for (guint i = 0; i < metadata->len; i++) {
			XbNode *c = g_ptr_array_index(metadata, i);
			const gchar *value = xb_node_get_text(c);
			if (xb_node_get_element(c) == NULL)
				continue;
			fu_efi_load_option_set_metadata(self,
							xb_node_get_element(c),
							value != NULL ? value : "");
		}
	}

	/* success */
	return TRUE;
}

static void
fu_efi_load_option_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuEfiLoadOption *self = FU_EFI_LOAD_OPTION(firmware);

	fu_xmlb_builder_insert_kx(bn, "attrs", self->attrs);
	if (self->kind != FU_EFI_LOAD_OPTION_KIND_UNKNOWN) {
		fu_xmlb_builder_insert_kv(bn,
					  "kind",
					  fu_efi_load_option_kind_to_string(self->kind));
	}
	if (g_hash_table_size(self->metadata) > 0) {
		GHashTableIter iter;
		const gchar *key;
		const gchar *value;
		g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "metadata", NULL);
		g_hash_table_iter_init(&iter, self->metadata);
		while (g_hash_table_iter_next(&iter, (gpointer *)&key, (gpointer *)&value))
			xb_builder_node_insert_text(bc, key, value, NULL);
	}
	if (self->optional_data != NULL) {
		gsize bufsz = 0;
		const guint8 *buf = g_bytes_get_data(self->optional_data, &bufsz);
		g_autofree gchar *datastr = g_base64_encode(buf, bufsz);
		xb_builder_node_insert_text(bn, "optional_data", datastr, NULL);
	}
}

static void
fu_efi_load_option_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuEfiLoadOption *self = FU_EFI_LOAD_OPTION(codec);
	GHashTableIter iter;
	const gchar *key;
	const gchar *value;
	g_autoptr(FuFirmware) dp_list = NULL;

	fwupd_codec_json_append(builder, "Name", fu_firmware_get_id(FU_FIRMWARE(self)));
	if (self->kind != FU_EFI_LOAD_OPTION_KIND_UNKNOWN) {
		fwupd_codec_json_append(builder,
					"Kind",
					fu_efi_load_option_kind_to_string(self->kind));
	}
	g_hash_table_iter_init(&iter, self->metadata);
	while (g_hash_table_iter_next(&iter, (gpointer *)&key, (gpointer *)&value))
		fwupd_codec_json_append(builder, key, value);
	dp_list =
	    fu_firmware_get_image_by_gtype(FU_FIRMWARE(self), FU_TYPE_EFI_DEVICE_PATH_LIST, NULL);
	if (dp_list != NULL)
		fwupd_codec_to_json(FWUPD_CODEC(dp_list), builder, flags);
}

static void
fu_efi_load_option_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_efi_load_option_add_json;
}

static void
fu_efi_load_option_finalize(GObject *obj)
{
	FuEfiLoadOption *self = FU_EFI_LOAD_OPTION(obj);
	if (self->optional_data != NULL)
		g_bytes_unref(self->optional_data);
	g_hash_table_unref(self->metadata);
	G_OBJECT_CLASS(fu_efi_load_option_parent_class)->finalize(obj);
}

static void
fu_efi_load_option_class_init(FuEfiLoadOptionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_efi_load_option_finalize;
	firmware_class->parse = fu_efi_load_option_parse;
	firmware_class->write = fu_efi_load_option_write;
	firmware_class->build = fu_efi_load_option_build;
	firmware_class->export = fu_efi_load_option_export;
}

static void
fu_efi_load_option_init(FuEfiLoadOption *self)
{
	self->attrs = FU_EFI_LOAD_OPTION_ATTRS_ACTIVE;
	self->metadata = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	g_type_ensure(FU_TYPE_EFI_DEVICE_PATH_LIST);
}

/**
 * fu_efi_load_option_new:
 *
 * Returns: (transfer full): a #FuEfiLoadOption
 *
 * Since: 1.9.3
 **/
FuEfiLoadOption *
fu_efi_load_option_new(void)
{
	return g_object_new(FU_TYPE_EFI_LOAD_OPTION, NULL);
}
