/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#ifdef HAVE_CBOR
#include <cbor.h>
#endif

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-coswid-firmware.h"
#include "fu-coswid-struct.h"

/**
 * FuCoswidFirmware:
 *
 * A coSWID SWID section.
 *
 * See also: [class@FuCoswidFirmware]
 */

typedef struct {
	gchar *product;
	gchar *summary;
	gchar *colloquial_version;
	FuCoswidVersionScheme version_scheme;
	GPtrArray *links;    /* of FuCoswidFirmwareLink */
	GPtrArray *entities; /* of FuCoswidFirmwareEntity */
	GPtrArray *payloads; /* of FuCoswidFirmwarePayload */
} FuCoswidFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuCoswidFirmware, fu_coswid_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_coswid_firmware_get_instance_private(o))

typedef struct {
	gchar *name;
	gchar *regid;
	FuCoswidEntityRole roles[6];
} FuCoswidFirmwareEntity;

typedef struct {
	gchar *href;
	FuCoswidLinkRel rel;
} FuCoswidFirmwareLink;

typedef struct {
	GByteArray *value;
	FuCoswidHashAlg alg_id;
} FuCoswidFirmwareHash;

typedef struct {
	gchar *name;
	guint64 size;
	GPtrArray *hashes; /* of FuCoswidFirmwareHash */
} FuCoswidFirmwarePayload;

static void
fu_coswid_firmware_entity_free(FuCoswidFirmwareEntity *entity)
{
	g_free(entity->name);
	g_free(entity->regid);
	g_free(entity);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCoswidFirmwareEntity, fu_coswid_firmware_entity_free)

static void
fu_coswid_firmware_link_free(FuCoswidFirmwareLink *link)
{
	g_free(link->href);
	g_free(link);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCoswidFirmwareLink, fu_coswid_firmware_link_free)

static void
fu_coswid_firmware_hash_free(FuCoswidFirmwareHash *hash)
{
	if (hash->value != NULL)
		g_byte_array_unref(hash->value);
	g_free(hash);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCoswidFirmwareHash, fu_coswid_firmware_hash_free)

static FuCoswidFirmwarePayload *
fu_coswid_firmware_payload_new(void)
{
	FuCoswidFirmwarePayload *payload = g_new0(FuCoswidFirmwarePayload, 1);
	payload->hashes =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_coswid_firmware_hash_free);
	return payload;
}

static void
fu_coswid_firmware_payload_free(FuCoswidFirmwarePayload *payload)
{
	g_ptr_array_unref(payload->hashes);
	g_free(payload->name);
	g_free(payload);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCoswidFirmwarePayload, fu_coswid_firmware_payload_free)

#ifdef HAVE_CBOR
G_DEFINE_AUTOPTR_CLEANUP_FUNC(cbor_item_t, cbor_intermediate_decref)

static gchar *
fu_coswid_firmware_strndup(cbor_item_t *item)
{
	if (!cbor_string_is_definite(item))
		return NULL;
	return g_strndup((const gchar *)cbor_string_handle(item), cbor_string_length(item));
}

typedef gboolean (*FuCoswidFirmwareItemFunc)(FuCoswidFirmware *self,
					     cbor_item_t *item,
					     gpointer user_data,
					     GError **error) G_GNUC_WARN_UNUSED_RESULT;

static gboolean
fu_coswid_firmware_parse_one_or_many(FuCoswidFirmware *self,
				     cbor_item_t *item,
				     FuCoswidFirmwareItemFunc func,
				     gpointer user_data,
				     GError **error)
{
	/* one */
	if (cbor_isa_map(item))
		return func(self, item, user_data, error);

	/* many */
	if (cbor_isa_array(item)) {
		for (guint j = 0; j < cbor_array_size(item); j++) {
			g_autoptr(cbor_item_t) value = cbor_array_get(item, j);
			if (!func(self, value, user_data, error))
				return FALSE;
		}
		return TRUE;
	}

	/* not sure what to do */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "neither an array or map");
	return FALSE;
}

static gboolean
fu_coswid_firmware_parse_meta(FuCoswidFirmware *self,
			      cbor_item_t *item,
			      gpointer user_data,
			      GError **error)
{
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	struct cbor_pair *pairs = cbor_map_handle(item);

	for (gsize i = 0; i < cbor_map_size(item); i++) {
		FuCoswidTag tag_id = cbor_get_uint8(pairs[i].key);
		if (tag_id == FU_COSWID_TAG_SUMMARY) {
			priv->summary = fu_coswid_firmware_strndup(pairs[i].value);
		} else if (tag_id == FU_COSWID_TAG_COLLOQUIAL_VERSION) {
			priv->colloquial_version = fu_coswid_firmware_strndup(pairs[i].value);
		} else {
			g_debug("unhandled tag %s from %s",
				fu_coswid_tag_to_string(tag_id),
				fu_coswid_tag_to_string(FU_COSWID_TAG_SOFTWARE_META));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_coswid_firmware_parse_link(FuCoswidFirmware *self,
			      cbor_item_t *item,
			      gpointer user_data,
			      GError **error)
{
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	struct cbor_pair *pairs = cbor_map_handle(item);
	g_autoptr(FuCoswidFirmwareLink) link = g_new0(FuCoswidFirmwareLink, 1);

	for (gsize i = 0; i < cbor_map_size(item); i++) {
		FuCoswidTag tag_id = cbor_get_uint8(pairs[i].key);
		if (tag_id == FU_COSWID_TAG_HREF) {
			link->href = fu_coswid_firmware_strndup(pairs[i].value);
		} else if (tag_id == FU_COSWID_TAG_REL) {
			if (cbor_isa_negint(pairs[i].value))
				link->rel = (-1) - cbor_get_uint8(pairs[i].value);
			else
				link->rel = cbor_get_uint8(pairs[i].value);
		} else {
			g_debug("unhandled tag %s from %s",
				fu_coswid_tag_to_string(tag_id),
				fu_coswid_tag_to_string(FU_COSWID_TAG_LINK));
		}
	}

	/* success */
	g_ptr_array_add(priv->links, g_steal_pointer(&link));
	return TRUE;
}

static gboolean
fu_coswid_firmware_parse_hash(FuCoswidFirmware *self,
			      cbor_item_t *item,
			      gpointer user_data,
			      GError **error)
{
	FuCoswidFirmwarePayload *payload = (FuCoswidFirmwarePayload *)user_data;
	g_autoptr(FuCoswidFirmwareHash) hash = g_new0(FuCoswidFirmwareHash, 1);
	g_autoptr(cbor_item_t) hash_item_alg_id = cbor_array_get(item, 0);
	g_autoptr(cbor_item_t) hash_item_value = cbor_array_get(item, 1);

	/* sanity check */
	if (hash_item_alg_id == NULL || hash_item_value == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "invalid hash item");
		return FALSE;
	}

	/* success */
	hash->alg_id = cbor_get_uint8(hash_item_alg_id);
	hash->value = g_byte_array_new();
	g_byte_array_append(hash->value,
			    cbor_bytestring_handle(hash_item_value),
			    cbor_bytestring_length(hash_item_value));
	g_ptr_array_add(payload->hashes, g_steal_pointer(&hash));
	return TRUE;
}

static gboolean
fu_coswid_firmware_parse_hash_array(FuCoswidFirmware *self,
				    cbor_item_t *item,
				    gpointer user_data,
				    GError **error)
{
	for (guint j = 0; j < cbor_array_size(item); j++) {
		g_autoptr(cbor_item_t) value = cbor_array_get(item, j);
		if (!fu_coswid_firmware_parse_hash(self, value, user_data, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_coswid_firmware_parse_file(FuCoswidFirmware *self,
			      cbor_item_t *item,
			      gpointer user_data,
			      GError **error)
{
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	struct cbor_pair *pairs = cbor_map_handle(item);
	g_autoptr(FuCoswidFirmwarePayload) payload = fu_coswid_firmware_payload_new();

	for (gsize i = 0; i < cbor_map_size(item); i++) {
		FuCoswidTag tag_id = cbor_get_uint8(pairs[i].key);
		if (tag_id == FU_COSWID_TAG_FS_NAME) {
			payload->name = fu_coswid_firmware_strndup(pairs[i].value);
		} else if (tag_id == FU_COSWID_TAG_SIZE) {
			payload->size = cbor_get_int(pairs[i].value);
		} else if (tag_id == FU_COSWID_TAG_HASH) {
			if (cbor_isa_array(pairs[i].value) &&
			    cbor_array_size(pairs[i].value) >= 1) {
				g_autoptr(cbor_item_t) value = cbor_array_get(pairs[i].value, 0);
				/* we can't use fu_coswid_firmware_parse_one_or_many() here as
				 * the hash is an array, not a map -- for some reason */
				if (cbor_isa_array(value)) {
					if (!fu_coswid_firmware_parse_hash_array(self,
										 pairs[i].value,
										 payload,
										 error))
						return FALSE;
				} else {
					if (!fu_coswid_firmware_parse_hash(self,
									   pairs[i].value,
									   payload,
									   error))
						return FALSE;
				}
			} else {
				g_set_error(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "hashes neither an array or array of array");
				return FALSE;
			}
		} else {
			g_debug("unhandled tag %s from %s",
				fu_coswid_tag_to_string(tag_id),
				fu_coswid_tag_to_string(FU_COSWID_TAG_FILE));
		}
	}

	/* success */
	g_ptr_array_add(priv->payloads, g_steal_pointer(&payload));
	return TRUE;
}

static gboolean
fu_coswid_firmware_parse_path_elements(FuCoswidFirmware *self,
				       cbor_item_t *item,
				       gpointer user_data,
				       GError **error)
{
	struct cbor_pair *pairs = cbor_map_handle(item);
	for (gsize i = 0; i < cbor_map_size(item); i++) {
		FuCoswidTag tag_id = cbor_get_uint8(pairs[i].key);
		if (tag_id == FU_COSWID_TAG_FILE) {
			if (!fu_coswid_firmware_parse_one_or_many(self,
								  pairs[i].value,
								  fu_coswid_firmware_parse_file,
								  NULL, /* user_data */
								  error))
				return FALSE;
		} else {
			g_debug("unhandled tag %s from %s",
				fu_coswid_tag_to_string(tag_id),
				fu_coswid_tag_to_string(FU_COSWID_TAG_PATH_ELEMENTS));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_coswid_firmware_parse_directory(FuCoswidFirmware *self,
				   cbor_item_t *item,
				   gpointer user_data,
				   GError **error)
{
	struct cbor_pair *pairs = cbor_map_handle(item);
	for (gsize i = 0; i < cbor_map_size(item); i++) {
		FuCoswidTag tag_id = cbor_get_uint8(pairs[i].key);
		if (tag_id == FU_COSWID_TAG_PATH_ELEMENTS) {
			if (!fu_coswid_firmware_parse_one_or_many(
				self,
				pairs[i].value,
				fu_coswid_firmware_parse_path_elements,
				NULL, /* user_data */
				error))
				return FALSE;
		} else {
			g_debug("unhandled tag %s from %s",
				fu_coswid_tag_to_string(tag_id),
				fu_coswid_tag_to_string(FU_COSWID_TAG_DIRECTORY));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_coswid_firmware_parse_payload(FuCoswidFirmware *self,
				 cbor_item_t *item,
				 gpointer user_data,
				 GError **error)
{
	struct cbor_pair *pairs = cbor_map_handle(item);
	for (gsize i = 0; i < cbor_map_size(item); i++) {
		FuCoswidTag tag_id = cbor_get_uint8(pairs[i].key);
		if (tag_id == FU_COSWID_TAG_FILE) {
			if (!fu_coswid_firmware_parse_one_or_many(self,
								  pairs[i].value,
								  fu_coswid_firmware_parse_file,
								  NULL, /* user_data */
								  error))
				return FALSE;
		} else if (tag_id == FU_COSWID_TAG_DIRECTORY) {
			if (!fu_coswid_firmware_parse_one_or_many(
				self,
				pairs[i].value,
				fu_coswid_firmware_parse_directory,
				NULL, /* user_data */
				error))
				return FALSE;
		} else {
			g_debug("unhandled tag %s from %s",
				fu_coswid_tag_to_string(tag_id),
				fu_coswid_tag_to_string(FU_COSWID_TAG_PAYLOAD));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_coswid_firmware_parse_entity(FuCoswidFirmware *self,
				cbor_item_t *item,
				gpointer user_data,
				GError **error)
{
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	struct cbor_pair *pairs = cbor_map_handle(item);
	guint entity_role_cnt = 0;
	g_autoptr(FuCoswidFirmwareEntity) entity = g_new0(FuCoswidFirmwareEntity, 1);

	for (gsize i = 0; i < cbor_map_size(item); i++) {
		FuCoswidTag tag_id = cbor_get_uint8(pairs[i].key);
		if (tag_id == FU_COSWID_TAG_ENTITY_NAME) {
			entity->name = fu_coswid_firmware_strndup(pairs[i].value);
		} else if (tag_id == FU_COSWID_TAG_REG_ID) {
			entity->regid = fu_coswid_firmware_strndup(pairs[i].value);
		} else if (tag_id == FU_COSWID_TAG_ROLE) {
			if (cbor_isa_uint(pairs[i].value)) {
				FuCoswidEntityRole role = cbor_get_uint8(pairs[i].value);
				entity->roles[entity_role_cnt++] = role;
			}
			for (guint j = 0; j < cbor_array_size(pairs[i].value); j++) {
				g_autoptr(cbor_item_t) value = cbor_array_get(pairs[i].value, j);
				FuCoswidEntityRole role = cbor_get_uint8(value);
				if (entity_role_cnt >= G_N_ELEMENTS(entity->roles)) {
					g_set_error_literal(error,
							    G_IO_ERROR,
							    G_IO_ERROR_INVALID_DATA,
							    "too many roles");
					return FALSE;
				}
				entity->roles[entity_role_cnt++] = role;
			}
		} else {
			g_debug("unhandled tag %s from %s",
				fu_coswid_tag_to_string(tag_id),
				fu_coswid_tag_to_string(FU_COSWID_TAG_ENTITY));
		}
	}

	/* success */
	g_ptr_array_add(priv->entities, g_steal_pointer(&entity));
	return TRUE;
}
#endif

static gboolean
fu_coswid_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
#ifdef HAVE_CBOR
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(firmware);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	struct cbor_load_result result = {0x0};
	struct cbor_pair *pairs = NULL;
	g_autoptr(cbor_item_t) item = NULL;

	item = cbor_load(g_bytes_get_data(fw, NULL), g_bytes_get_size(fw), &result);
	if (item == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to parse CBOR at offset 0x%x: 0x%x",
			    (guint)result.error.position,
			    result.error.code);
		return FALSE;
	}
	fu_firmware_set_size(firmware, result.read);

	/* pretty-print the result */
	if (g_getenv("FWUPD_CBOR_VERBOSE") != NULL) {
		cbor_describe(item, stdout);
		fflush(stdout);
	}

	/* sanity check */
	if (!cbor_isa_map(item)) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "root item is not a map");
		return FALSE;
	}

	/* parse out anything interesting */
	pairs = cbor_map_handle(item);
	for (gsize i = 0; i < cbor_map_size(item); i++) {
		FuCoswidTag tag_id = cbor_get_uint8(pairs[i].key);

		/* identity can be specified as a string or in binary */
		if (tag_id == FU_COSWID_TAG_TAG_ID) {
			g_autofree gchar *str = NULL;
			if (cbor_isa_string(pairs[i].value)) {
				str = fu_coswid_firmware_strndup(pairs[i].value);
			} else if (cbor_isa_bytestring(pairs[i].value) &&
				   cbor_bytestring_length(pairs[i].value) == 16) {
				str = fwupd_guid_to_string(
				    (const fwupd_guid_t *)cbor_bytestring_handle(pairs[i].value),
				    FWUPD_GUID_FLAG_NONE);
			}
			if (str != NULL)
				fu_firmware_set_id(firmware, str);
		} else if (tag_id == FU_COSWID_TAG_SOFTWARE_NAME) {
			priv->product = fu_coswid_firmware_strndup(pairs[i].value);
		} else if (tag_id == FU_COSWID_TAG_SOFTWARE_VERSION) {
			g_autofree gchar *str = fu_coswid_firmware_strndup(pairs[i].value);
			fu_firmware_set_version(firmware, str);
		} else if (tag_id == FU_COSWID_TAG_VERSION_SCHEME) {
			priv->version_scheme = cbor_get_uint16(pairs[i].value);
		} else if (tag_id == FU_COSWID_TAG_SOFTWARE_META) {
			if (!fu_coswid_firmware_parse_one_or_many(self,
								  pairs[i].value,
								  fu_coswid_firmware_parse_meta,
								  NULL, /* user_data */
								  error))
				return FALSE;
		} else if (tag_id == FU_COSWID_TAG_LINK) {
			if (!fu_coswid_firmware_parse_one_or_many(self,
								  pairs[i].value,
								  fu_coswid_firmware_parse_link,
								  NULL, /* user_data */
								  error))
				return FALSE;
		} else if (tag_id == FU_COSWID_TAG_PAYLOAD) {
			if (!fu_coswid_firmware_parse_one_or_many(self,
								  pairs[i].value,
								  fu_coswid_firmware_parse_payload,
								  NULL, /* user_data */
								  error))
				return FALSE;
		} else if (tag_id == FU_COSWID_TAG_ENTITY) {
			if (!fu_coswid_firmware_parse_one_or_many(self,
								  pairs[i].value,
								  fu_coswid_firmware_parse_entity,
								  NULL, /* user_data */
								  error))
				return FALSE;
		} else {
			g_debug("unhandled tag %s from root", fu_coswid_tag_to_string(tag_id));
		}
	}

	/* device not supported */
	if (fu_firmware_get_id(firmware) == NULL && fu_firmware_get_version(firmware) == NULL &&
	    priv->product == NULL && priv->entities->len == 0 && priv->links->len == 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "not enough SBoM data");
		return FALSE;
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "not compiled with CBOR support");
	return FALSE;
#endif
}

static gchar *
fu_coswid_firmware_get_checksum(FuFirmware *firmware, GChecksumType csum_kind, GError **error)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(firmware);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	FuCoswidHashAlg alg_id = FU_COSWID_HASH_ALG_UNKNOWN;
	struct {
		GChecksumType kind;
		FuCoswidHashAlg alg_id;
	} csum_kinds[] = {{G_CHECKSUM_SHA256, FU_COSWID_HASH_ALG_SHA256},
			  {G_CHECKSUM_SHA384, FU_COSWID_HASH_ALG_SHA384},
			  {G_CHECKSUM_SHA512, FU_COSWID_HASH_ALG_SHA512},
			  {0, FU_COSWID_HASH_ALG_UNKNOWN}};

	/* convert to FuCoswidHashAlg */
	for (guint i = 0; csum_kinds[i].kind != 0; i++) {
		if (csum_kinds[i].kind == csum_kind) {
			alg_id = csum_kinds[i].alg_id;
			break;
		}
	}
	if (alg_id == FU_COSWID_HASH_ALG_UNKNOWN) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "cannot convert %s",
			    fwupd_checksum_type_to_string_display(csum_kind));
		return NULL;
	}

	/* find the correct hash kind */
	for (guint i = 0; i < priv->payloads->len; i++) {
		FuCoswidFirmwarePayload *payload = g_ptr_array_index(priv->payloads, i);
		for (guint j = 0; j < payload->hashes->len; j++) {
			FuCoswidFirmwareHash *hash = g_ptr_array_index(payload->hashes, j);
			if (hash->alg_id == alg_id)
				return fu_byte_array_to_string(hash->value);
		}
	}
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_NOT_SUPPORTED,
		    "no hash kind %s",
		    fwupd_checksum_type_to_string_display(csum_kind));
	return NULL;
}

#ifdef HAVE_CBOR
static void
fu_coswid_firmware_write_tag_string(cbor_item_t *root, FuCoswidTag tag, const gchar *item)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	g_autoptr(cbor_item_t) val = cbor_build_string(item);
	if (!cbor_map_add(root, (struct cbor_pair){.key = key, .value = val}))
		g_critical("failed to push to indefinite map");
}

static void
fu_coswid_firmware_write_tag_bytestring(cbor_item_t *root,
					FuCoswidTag tag,
					const guint8 *buf,
					gsize bufsz)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	g_autoptr(cbor_item_t) val = cbor_build_bytestring((cbor_data)buf, bufsz);
	if (!cbor_map_add(root, (struct cbor_pair){.key = key, .value = val}))
		g_critical("failed to push to indefinite map");
}

static void
fu_coswid_firmware_write_tag_bool(cbor_item_t *root, FuCoswidTag tag, gboolean item)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	g_autoptr(cbor_item_t) val = cbor_build_bool(item);
	if (!cbor_map_add(root, (struct cbor_pair){.key = key, .value = val}))
		g_critical("failed to push to indefinite map");
}

static void
fu_coswid_firmware_write_tag_uint16(cbor_item_t *root, FuCoswidTag tag, guint16 item)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	g_autoptr(cbor_item_t) val = cbor_build_uint16(item);
	if (!cbor_map_add(root, (struct cbor_pair){.key = key, .value = val}))
		g_critical("failed to push to indefinite map");
}

static void
fu_coswid_firmware_write_tag_uint64(cbor_item_t *root, FuCoswidTag tag, guint64 item)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	g_autoptr(cbor_item_t) val = cbor_build_uint64(item);
	if (!cbor_map_add(root, (struct cbor_pair){.key = key, .value = val}))
		g_critical("failed to push to indefinite map");
}

static void
fu_coswid_firmware_write_tag_int8(cbor_item_t *root, FuCoswidTag tag, gint8 item)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	g_autoptr(cbor_item_t) val = cbor_new_int8();
	if (item >= 0) {
		cbor_set_uint8(val, item);
	} else {
		cbor_set_uint8(val, 0xFF - item);
		cbor_mark_negint(val);
	}
	if (!cbor_map_add(root, (struct cbor_pair){.key = key, .value = val}))
		g_critical("failed to push to indefinite map");
}

static void
fu_coswid_firmware_write_tag_item(cbor_item_t *root, FuCoswidTag tag, cbor_item_t *item)
{
	g_autoptr(cbor_item_t) key = cbor_build_uint8(tag);
	if (!cbor_map_add(root, (struct cbor_pair){.key = key, .value = item}))
		g_critical("failed to push to indefinite map");
}

static void
fu_coswid_firmware_write_hash(cbor_item_t *root, FuCoswidFirmwareHash *hash)
{
	g_autoptr(cbor_item_t) item_hash = cbor_new_definite_array(2);
	g_autoptr(cbor_item_t) item_hash_alg_id = cbor_build_uint8(hash->alg_id);
	g_autoptr(cbor_item_t) item_hash_value =
	    cbor_build_bytestring(hash->value->data, hash->value->len);
	if (!cbor_array_push(item_hash, item_hash_alg_id))
		g_critical("failed to push to definite array");
	if (!cbor_array_push(item_hash, item_hash_value))
		g_critical("failed to push to definite array");
	if (!cbor_array_push(root, item_hash))
		g_critical("failed to push to indefinite array");
}

static void
fu_coswid_firmware_write_payload(cbor_item_t *root, FuCoswidFirmwarePayload *payload)
{
	g_autoptr(cbor_item_t) item_payload = cbor_new_indefinite_map();
	g_autoptr(cbor_item_t) item_file = cbor_new_indefinite_map();
	if (payload->name != NULL) {
		fu_coswid_firmware_write_tag_string(item_file,
						    FU_COSWID_TAG_FS_NAME,
						    payload->name);
	}
	if (payload->size != 0) {
		fu_coswid_firmware_write_tag_uint64(item_file, FU_COSWID_TAG_SIZE, payload->size);
	}
	if (payload->hashes->len > 0) {
		g_autoptr(cbor_item_t) item_hashes = cbor_new_indefinite_array();
		for (guint j = 0; j < payload->hashes->len; j++) {
			FuCoswidFirmwareHash *hash = g_ptr_array_index(payload->hashes, j);
			fu_coswid_firmware_write_hash(item_hashes, hash);
		}
		fu_coswid_firmware_write_tag_item(item_file, FU_COSWID_TAG_HASH, item_hashes);
	}
	fu_coswid_firmware_write_tag_item(item_payload, FU_COSWID_TAG_FILE, item_file);
	if (!cbor_array_push(root, item_payload))
		g_critical("failed to push to indefinite array");
}
#endif

static GByteArray *
fu_coswid_firmware_write(FuFirmware *firmware, GError **error)
{
#ifdef HAVE_CBOR
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(firmware);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize buflen;
	gsize bufsz = 0;
	g_autofree guchar *buf = NULL;
	g_autoptr(cbor_item_t) root = cbor_new_indefinite_map();
	g_autoptr(cbor_item_t) item_meta = cbor_new_indefinite_map();

	/* preallocate the map structure */
	fu_coswid_firmware_write_tag_string(root, FU_COSWID_TAG_LANG, "en-US");
	if (fu_firmware_get_id(firmware) != NULL) {
		fwupd_guid_t uuid = {0};
		if (fwupd_guid_from_string(fu_firmware_get_id(firmware),
					   &uuid,
					   FWUPD_GUID_FLAG_NONE,
					   NULL)) {
			fu_coswid_firmware_write_tag_bytestring(root,
								FU_COSWID_TAG_TAG_ID,
								(const guint8 *)&uuid,
								sizeof(uuid));
		} else {
			fu_coswid_firmware_write_tag_string(root,
							    FU_COSWID_TAG_TAG_ID,
							    fu_firmware_get_id(firmware));
		}
	}
	fu_coswid_firmware_write_tag_bool(root, FU_COSWID_TAG_CORPUS, TRUE);
	if (priv->product != NULL) {
		fu_coswid_firmware_write_tag_string(root,
						    FU_COSWID_TAG_SOFTWARE_NAME,
						    priv->product);
	}
	if (fu_firmware_get_version(firmware) != NULL) {
		fu_coswid_firmware_write_tag_string(root,
						    FU_COSWID_TAG_SOFTWARE_VERSION,
						    fu_firmware_get_version(firmware));
	}
	if (priv->version_scheme != FU_COSWID_VERSION_SCHEME_UNKNOWN) {
		fu_coswid_firmware_write_tag_uint16(root,
						    FU_COSWID_TAG_VERSION_SCHEME,
						    priv->version_scheme);
	}
	fu_coswid_firmware_write_tag_item(root, FU_COSWID_TAG_SOFTWARE_META, item_meta);
	fu_coswid_firmware_write_tag_string(item_meta, FU_COSWID_TAG_GENERATOR, PACKAGE_NAME);
	if (priv->summary != NULL) {
		fu_coswid_firmware_write_tag_string(item_meta,
						    FU_COSWID_TAG_SUMMARY,
						    priv->summary);
	}
	if (priv->colloquial_version != NULL) {
		fu_coswid_firmware_write_tag_string(item_meta,
						    FU_COSWID_TAG_COLLOQUIAL_VERSION,
						    priv->colloquial_version);
	}

	/* add entities */
	if (priv->entities->len > 0) {
		g_autoptr(cbor_item_t) item_entities = cbor_new_indefinite_array();
		for (guint i = 0; i < priv->entities->len; i++) {
			FuCoswidFirmwareEntity *entity = g_ptr_array_index(priv->entities, i);
			g_autoptr(cbor_item_t) item_entity = cbor_new_indefinite_map();
			g_autoptr(cbor_item_t) item_roles = cbor_new_indefinite_array();
			if (entity->name != NULL) {
				fu_coswid_firmware_write_tag_string(item_entity,
								    FU_COSWID_TAG_ENTITY_NAME,
								    entity->name);
			}
			if (entity->regid != NULL) {
				fu_coswid_firmware_write_tag_string(item_entity,
								    FU_COSWID_TAG_REG_ID,
								    entity->regid);
			}
			for (guint j = 0; entity->roles[j] != FU_COSWID_ENTITY_ROLE_UNKNOWN; j++) {
				g_autoptr(cbor_item_t) item_role =
				    cbor_build_uint8(entity->roles[j]);
				if (!cbor_array_push(item_roles, item_role))
					g_critical("failed to push to indefinite array");
			}
			fu_coswid_firmware_write_tag_item(item_entity,
							  FU_COSWID_TAG_ROLE,
							  item_roles);
			if (!cbor_array_push(item_entities, item_entity))
				g_critical("failed to push to indefinite array");
		}
		fu_coswid_firmware_write_tag_item(root, FU_COSWID_TAG_ENTITY, item_entities);
	}

	/* add links */
	if (priv->links->len > 0) {
		g_autoptr(cbor_item_t) item_links = cbor_new_indefinite_array();
		for (guint i = 0; i < priv->links->len; i++) {
			FuCoswidFirmwareLink *link = g_ptr_array_index(priv->links, i);
			g_autoptr(cbor_item_t) item_link = cbor_new_indefinite_map();
			if (link->href != NULL) {
				fu_coswid_firmware_write_tag_string(item_link,
								    FU_COSWID_TAG_HREF,
								    link->href);
			}
			fu_coswid_firmware_write_tag_int8(item_link, FU_COSWID_TAG_REL, link->rel);
			if (!cbor_array_push(item_links, item_link))
				g_critical("failed to push to indefinite array");
		}
		fu_coswid_firmware_write_tag_item(root, FU_COSWID_TAG_LINK, item_links);
	}

	/* add payloads */
	if (priv->payloads->len > 0) {
		g_autoptr(cbor_item_t) item_payloads = cbor_new_indefinite_array();
		for (guint i = 0; i < priv->payloads->len; i++) {
			FuCoswidFirmwarePayload *payload = g_ptr_array_index(priv->payloads, i);
			fu_coswid_firmware_write_payload(item_payloads, payload);
		}
		fu_coswid_firmware_write_tag_item(root, FU_COSWID_TAG_PAYLOAD, item_payloads);
	}

	/* serialize */
	buflen = cbor_serialize_alloc(root, &buf, &bufsz);
	if (buflen > bufsz) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "CBOR allocation failure");
		return NULL;
	}
	return g_byte_array_new_take(g_steal_pointer(&buf), buflen);
#else
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "not compiled with CBOR support");
	return NULL;
#endif
}

static gboolean
fu_coswid_firmware_build_entity(FuCoswidFirmware *self, XbNode *n, GError **error)
{
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	guint entity_role_cnt = 0;
	FuCoswidEntityRole role;
	g_autoptr(GPtrArray) roles = NULL;
	g_autoptr(FuCoswidFirmwareEntity) entity = g_new0(FuCoswidFirmwareEntity, 1);

	/* these are required */
	tmp = xb_node_query_text(n, "name", error);
	if (tmp == NULL)
		return FALSE;
	entity->name = g_strdup(tmp);
	tmp = xb_node_query_text(n, "regid", error);
	if (tmp == NULL)
		return FALSE;
	entity->regid = g_strdup(tmp);

	/* optional */
	roles = xb_node_query(n, "role", 0, NULL);
	if (roles != NULL) {
		for (guint i = 0; i < roles->len; i++) {
			XbNode *c = g_ptr_array_index(roles, i);
			tmp = xb_node_get_text(c);
			role = fu_coswid_entity_role_from_string(tmp);
			if (role == FU_COSWID_ENTITY_ROLE_UNKNOWN) {
				g_set_error(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "failed to parse entity role %s",
					    tmp);
				return FALSE;
			}
			if (entity_role_cnt >= G_N_ELEMENTS(entity->roles)) {
				g_set_error_literal(error,
						    G_IO_ERROR,
						    G_IO_ERROR_INVALID_DATA,
						    "too many roles");
				return FALSE;
			}
			entity->roles[entity_role_cnt++] = role;
		}
	}

	/* success */
	g_ptr_array_add(priv->entities, g_steal_pointer(&entity));
	return TRUE;
}

static gboolean
fu_coswid_firmware_build_link(FuCoswidFirmware *self, XbNode *n, GError **error)
{
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	g_autoptr(FuCoswidFirmwareLink) link = g_new0(FuCoswidFirmwareLink, 1);

	/* required */
	tmp = xb_node_query_text(n, "href", error);
	if (tmp == NULL)
		return FALSE;
	link->href = g_strdup(tmp);

	/* optional */
	tmp = xb_node_query_text(n, "rel", NULL);
	if (tmp != NULL) {
		link->rel = fu_coswid_link_rel_from_string(tmp);
		if (link->rel == FU_COSWID_LINK_REL_UNKNOWN) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "failed to parse link rel %s",
				    tmp);
			return FALSE;
		}
	}

	/* success */
	g_ptr_array_add(priv->links, g_steal_pointer(&link));
	return TRUE;
}

static gboolean
fu_coswid_firmware_build_hash(FuCoswidFirmware *self,
			      XbNode *n,
			      FuCoswidFirmwarePayload *payload,
			      GError **error)
{
	const gchar *tmp;
	g_autoptr(FuCoswidFirmwareHash) hash = g_new0(FuCoswidFirmwareHash, 1);

	/* required */
	tmp = xb_node_query_text(n, "value", error);
	if (tmp == NULL)
		return FALSE;
	hash->value = fu_byte_array_from_string(tmp, error);
	if (hash->value == NULL)
		return FALSE;

	/* optional */
	tmp = xb_node_query_text(n, "alg_id", NULL);
	if (tmp != NULL) {
		hash->alg_id = fu_coswid_hash_alg_from_string(tmp);
		if (hash->alg_id == FU_COSWID_HASH_ALG_UNKNOWN) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "failed to parse alg_id %s",
				    tmp);
			return FALSE;
		}
	}

	/* success */
	g_ptr_array_add(payload->hashes, g_steal_pointer(&hash));
	return TRUE;
}

static gboolean
fu_coswid_firmware_build_payload(FuCoswidFirmware *self, XbNode *n, GError **error)
{
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	guint64 tmp64;
	g_autoptr(FuCoswidFirmwarePayload) payload = fu_coswid_firmware_payload_new();
	g_autoptr(GPtrArray) hashes = NULL;

	/* required */
	tmp = xb_node_query_text(n, "name", NULL);
	if (tmp != NULL)
		payload->name = g_strdup(tmp);
	tmp64 = xb_node_query_text_as_uint(n, "size", NULL);
	if (tmp64 != G_MAXUINT64)
		payload->size = tmp64;

	/* multiple hashes allowed */
	hashes = xb_node_query(n, "hash", 0, NULL);
	if (hashes != NULL) {
		for (guint i = 0; i < hashes->len; i++) {
			XbNode *c = g_ptr_array_index(hashes, i);
			if (!fu_coswid_firmware_build_hash(self, c, payload, error))
				return FALSE;
		}
	}

	/* success */
	g_ptr_array_add(priv->payloads, g_steal_pointer(&payload));
	return TRUE;
}

static gboolean
fu_coswid_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(firmware);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	g_autoptr(GPtrArray) links = NULL;
	g_autoptr(GPtrArray) payloads = NULL;
	g_autoptr(GPtrArray) entities = NULL;

	/* simple properties */
	tmp = xb_node_query_text(n, "product", NULL);
	if (tmp != NULL)
		priv->product = g_strdup(tmp);
	tmp = xb_node_query_text(n, "summary", NULL);
	if (tmp != NULL)
		priv->summary = g_strdup(tmp);
	tmp = xb_node_query_text(n, "colloquial_version", NULL);
	if (tmp != NULL)
		priv->colloquial_version = g_strdup(tmp);

	tmp = xb_node_query_text(n, "version_scheme", NULL);
	if (tmp != NULL) {
		priv->version_scheme = fu_coswid_version_scheme_from_string(tmp);
		if (priv->version_scheme == FU_COSWID_VERSION_SCHEME_UNKNOWN) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "failed to parse version_scheme %s",
				    tmp);
			return FALSE;
		}
	}

	/* multiple links allowed */
	links = xb_node_query(n, "link", 0, NULL);
	if (links != NULL) {
		for (guint i = 0; i < links->len; i++) {
			XbNode *c = g_ptr_array_index(links, i);
			if (!fu_coswid_firmware_build_link(self, c, error))
				return FALSE;
		}
	}

	/* multiple payloads allowed */
	payloads = xb_node_query(n, "payload", 0, NULL);
	if (payloads != NULL) {
		for (guint i = 0; i < payloads->len; i++) {
			XbNode *c = g_ptr_array_index(payloads, i);
			if (!fu_coswid_firmware_build_payload(self, c, error))
				return FALSE;
		}
	}

	/* multiple entities allowed */
	entities = xb_node_query(n, "entity", 0, NULL);
	if (entities != NULL) {
		for (guint i = 0; i < entities->len; i++) {
			XbNode *c = g_ptr_array_index(entities, i);
			if (!fu_coswid_firmware_build_entity(self, c, error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_coswid_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(firmware);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	if (priv->version_scheme != FU_COSWID_VERSION_SCHEME_UNKNOWN) {
		fu_xmlb_builder_insert_kv(bn,
					  "version_scheme",
					  fu_coswid_version_scheme_to_string(priv->version_scheme));
	}
	fu_xmlb_builder_insert_kv(bn, "product", priv->product);
	fu_xmlb_builder_insert_kv(bn, "summary", priv->summary);
	fu_xmlb_builder_insert_kv(bn, "colloquial_version", priv->colloquial_version);
	for (guint i = 0; i < priv->links->len; i++) {
		FuCoswidFirmwareLink *link = g_ptr_array_index(priv->links, i);
		g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "link", NULL);
		fu_xmlb_builder_insert_kv(bc, "href", link->href);
		if (link->rel != FU_COSWID_LINK_REL_UNKNOWN) {
			fu_xmlb_builder_insert_kv(bc,
						  "rel",
						  fu_coswid_link_rel_to_string(link->rel));
		}
	}
	for (guint i = 0; i < priv->payloads->len; i++) {
		FuCoswidFirmwarePayload *payload = g_ptr_array_index(priv->payloads, i);
		g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "payload", NULL);
		fu_xmlb_builder_insert_kv(bc, "name", payload->name);
		fu_xmlb_builder_insert_kx(bc, "size", payload->size);
		for (guint j = 0; j < payload->hashes->len; j++) {
			FuCoswidFirmwareHash *hash = g_ptr_array_index(payload->hashes, j);
			g_autoptr(XbBuilderNode) bh = xb_builder_node_insert(bc, "hash", NULL);
			g_autofree gchar *value = fu_byte_array_to_string(hash->value);
			fu_xmlb_builder_insert_kv(bh,
						  "alg_id",
						  fu_coswid_hash_alg_to_string(hash->alg_id));
			fu_xmlb_builder_insert_kv(bh, "value", value);
		}
	}
	for (guint i = 0; i < priv->entities->len; i++) {
		FuCoswidFirmwareEntity *entity = g_ptr_array_index(priv->entities, i);
		g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "entity", NULL);
		fu_xmlb_builder_insert_kv(bc, "name", entity->name);
		fu_xmlb_builder_insert_kv(bc, "regid", entity->regid);
		for (guint j = 0; entity->roles[j] != FU_COSWID_ENTITY_ROLE_UNKNOWN; j++) {
			fu_xmlb_builder_insert_kv(
			    bc,
			    "role",
			    fu_coswid_entity_role_to_string(entity->roles[j]));
		}
	}
}

static void
fu_coswid_firmware_init(FuCoswidFirmware *self)
{
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->version_scheme = FU_COSWID_VERSION_SCHEME_SEMVER;
	priv->links = g_ptr_array_new_with_free_func((GDestroyNotify)fu_coswid_firmware_link_free);
	priv->payloads =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_coswid_firmware_payload_free);
	priv->entities =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_coswid_firmware_entity_free);
}

static void
fu_coswid_firmware_finalize(GObject *object)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(object);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);

	g_free(priv->product);
	g_free(priv->summary);
	g_free(priv->colloquial_version);
	g_ptr_array_unref(priv->links);
	g_ptr_array_unref(priv->payloads);
	g_ptr_array_unref(priv->entities);

	G_OBJECT_CLASS(fu_coswid_firmware_parent_class)->finalize(object);
}

static void
fu_coswid_firmware_class_init(FuCoswidFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_coswid_firmware_finalize;
	klass_firmware->parse = fu_coswid_firmware_parse;
	klass_firmware->write = fu_coswid_firmware_write;
	klass_firmware->build = fu_coswid_firmware_build;
	klass_firmware->export = fu_coswid_firmware_export;
	klass_firmware->get_checksum = fu_coswid_firmware_get_checksum;
}

/**
 * fu_coswid_firmware_new:
 *
 * Creates a new #FuFirmware of sub type coSWID
 *
 * Since: 1.8.0
 **/
FuFirmware *
fu_coswid_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_COSWID_FIRMWARE, NULL));
}
