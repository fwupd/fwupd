/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-bytes.h"
#include "fu-cbor-common.h"
#include "fu-common.h"
#include "fu-coswid-common.h"
#include "fu-coswid-firmware.h"
#include "fu-coswid-struct.h"
#include "fu-input-stream.h"

/**
 * FuCoswidFirmware:
 *
 * A coSWID SWID section.
 *
 * See also: [class@FuUswidFirmware]
 */

typedef struct {
	gchar *product;
	gchar *summary;
	gchar *colloquial_version;
	gchar *persistent_id;
	gchar *device_id;
	FuCoswidVersionScheme version_scheme;
	GPtrArray *links;    /* of FuCoswidFirmwareLink */
	GPtrArray *entities; /* of FuCoswidFirmwareEntity */
	GPtrArray *payloads; /* of FuCoswidFirmwarePayload */
} FuCoswidFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuCoswidFirmware, fu_coswid_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_coswid_firmware_get_instance_private(o))

#define FU_COSWID_CBOR_MAX_DEPTH  10
#define FU_COSWID_CBOR_MAX_ITEMS  100
#define FU_COSWID_CBOR_MAX_LENGTH 10240

typedef struct {
	gchar *name;
	gchar *regid;
	guint8 roles; /* bitfield of FuCoswidEntityRole */
} FuCoswidFirmwareEntity;

typedef struct {
	gchar *href;
	FuCoswidLinkRel rel;
} FuCoswidFirmwareLink;

typedef struct {
	GBytes *value;
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
		g_bytes_unref(hash->value);
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

/* @userdata: a #FuCoswidFirmware */
static gboolean
fu_coswid_firmware_parse_meta(FuCborItem *item, gpointer user_data, GError **error)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(user_data);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);

	for (guint i = 0; i < fu_cbor_item_map_length(item); i++) {
		FuCoswidTag tag_id = 0;
		FuCborItem *item_key = NULL;
		FuCborItem *item_value = NULL;

		fu_cbor_item_map_index(item, i, &item_key, &item_value);
		if (!fu_coswid_read_tag(item_key, &tag_id, error)) {
			g_prefix_error(error, "failed to parse meta tag %u: ", (guint)i);
			return FALSE;
		}
		if (tag_id == FU_COSWID_TAG_SUMMARY) {
			g_free(priv->summary);
			priv->summary = fu_coswid_read_string(item_value, error);
			if (priv->summary == NULL) {
				g_prefix_error_literal(error, "failed to parse summary: ");
				return FALSE;
			}
		} else if (tag_id == FU_COSWID_TAG_COLLOQUIAL_VERSION) {
			g_free(priv->colloquial_version);
			priv->colloquial_version = fu_coswid_read_string(item_value, error);
			if (priv->colloquial_version == NULL) {
				g_prefix_error_literal(error,
						       "failed to parse colloquial-version: ");
				return FALSE;
			}
		} else if (tag_id == FU_COSWID_TAG_PERSISTENT_ID) {
			g_free(priv->persistent_id);
			priv->persistent_id = fu_coswid_read_string(item_value, error);
			if (priv->persistent_id == NULL) {
				g_prefix_error_literal(error, "failed to parse persistent-id: ");
				return FALSE;
			}
		} else {
			g_debug("unhandled tag %s from %s",
				fu_coswid_tag_to_string(tag_id),
				fu_coswid_tag_to_string(FU_COSWID_TAG_SOFTWARE_META));
		}
	}

	/* success */
	return TRUE;
}

/* @userdata: a #FuCoswidFirmware */
static gboolean
fu_coswid_firmware_parse_evidence(FuCborItem *item, gpointer user_data, GError **error)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(user_data);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);

	for (gsize i = 0; i < fu_cbor_item_map_length(item); i++) {
		FuCoswidTag tag_id = 0;
		FuCborItem *item_key = NULL;
		FuCborItem *item_value = NULL;

		fu_cbor_item_map_index(item, i, &item_key, &item_value);
		if (!fu_coswid_read_tag(item_key, &tag_id, error)) {
			g_prefix_error(error, "failed to parse evidence tag %u: ", (guint)i);
			return FALSE;
		}
		if (tag_id == FU_COSWID_TAG_DEVICE_ID) {
			g_free(priv->device_id);
			priv->device_id = fu_coswid_read_string(item_value, error);
			if (priv->device_id == NULL) {
				g_prefix_error_literal(error, "failed to parse device-id: ");
				return FALSE;
			}
		} else {
			g_debug("unhandled tag %s from %s",
				fu_coswid_tag_to_string(tag_id),
				fu_coswid_tag_to_string(FU_COSWID_TAG_SOFTWARE_META));
		}
	}

	/* success */
	return TRUE;
}

/* @userdata: a #FuCoswidFirmware */
static gboolean
fu_coswid_firmware_parse_link(FuCborItem *item, gpointer user_data, GError **error)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(user_data);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuCoswidFirmwareLink) link = g_new0(FuCoswidFirmwareLink, 1);

	for (gsize i = 0; i < fu_cbor_item_map_length(item); i++) {
		FuCoswidTag tag_id = 0;
		FuCborItem *item_key = NULL;
		FuCborItem *item_value = NULL;

		fu_cbor_item_map_index(item, i, &item_key, &item_value);
		if (!fu_coswid_read_tag(item_key, &tag_id, error)) {
			g_prefix_error(error, "failed to parse link tag %u: ", (guint)i);
			return FALSE;
		}
		if (tag_id == FU_COSWID_TAG_HREF) {
			g_free(link->href);
			link->href = fu_coswid_read_string(item_value, error);
			if (link->href == NULL) {
				g_prefix_error_literal(error, "failed to parse link href: ");
				return FALSE;
			}
		} else if (tag_id == FU_COSWID_TAG_REL) {
			gint64 tmp = 0;
			if (!fu_cbor_item_get_integer(item_value, &tmp, error)) {
				g_prefix_error_literal(error, "failed to parse link rel: ");
				return FALSE;
			}
			link->rel = tmp;
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

/* @userdata: a #FuCoswidFirmwarePayload */
static gboolean
fu_coswid_firmware_parse_hash(FuCborItem *item, gpointer user_data, GError **error)
{
	FuCoswidFirmwarePayload *payload = (FuCoswidFirmwarePayload *)user_data;
	gint64 alg_id8 = 0;
	g_autoptr(FuCoswidFirmwareHash) hash = g_new0(FuCoswidFirmwareHash, 1);
	FuCborItem *hash_item_alg_id;
	FuCborItem *hash_item_value;

	/* sanity check */
	if (fu_cbor_item_get_kind(item) != FU_CBOR_ITEM_KIND_ARRAY) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "hash item is not an array");
		return FALSE;
	}
	if (fu_cbor_item_array_length(item) != 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "hash array has invalid size");
		return FALSE;
	}
	hash_item_alg_id = fu_cbor_item_array_index(item, 0);
	hash_item_value = fu_cbor_item_array_index(item, 1);
	if (hash_item_alg_id == NULL || hash_item_value == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid hash item");
		return FALSE;
	}
	if (!fu_cbor_item_get_integer(hash_item_alg_id, &alg_id8, error)) {
		g_prefix_error_literal(error, "failed to parse hash alg-id: ");
		return FALSE;
	}

	/* success */
	hash->alg_id = alg_id8;
	hash->value = fu_cbor_item_get_bytes(hash_item_value, error);
	if (hash->value == NULL) {
		g_prefix_error_literal(error, "failed to parse hash value: ");
		return FALSE;
	}
	g_ptr_array_add(payload->hashes, g_steal_pointer(&hash));
	return TRUE;
}

/* @userdata: a #FuCoswidFirmwarePayload */
static gboolean
fu_coswid_firmware_parse_hash_array(FuCborItem *item, gpointer user_data, GError **error)
{
	for (guint j = 0; j < fu_cbor_item_array_length(item); j++) {
		FuCborItem *value = fu_cbor_item_array_index(item, j);
		if (!fu_coswid_firmware_parse_hash(value, user_data, error))
			return FALSE;
	}
	return TRUE;
}

/* @userdata: a #FuCoswidFirmware */
static gboolean
fu_coswid_firmware_parse_file(FuCborItem *item, gpointer user_data, GError **error)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(user_data);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuCoswidFirmwarePayload) payload = fu_coswid_firmware_payload_new();

	for (gsize i = 0; i < fu_cbor_item_map_length(item); i++) {
		FuCoswidTag tag_id = 0;
		FuCborItem *item_key = NULL;
		FuCborItem *item_value = NULL;

		fu_cbor_item_map_index(item, i, &item_key, &item_value);
		if (!fu_coswid_read_tag(item_key, &tag_id, error)) {
			g_prefix_error(error, "failed to parse file tag %u: ", (guint)i);
			return FALSE;
		}
		if (tag_id == FU_COSWID_TAG_FS_NAME) {
			g_free(payload->name);
			payload->name = fu_coswid_read_string(item_value, error);
			if (payload->name == NULL) {
				g_prefix_error_literal(error, "failed to parse payload name: ");
				return FALSE;
			}
		} else if (tag_id == FU_COSWID_TAG_SIZE) {
			gint64 tmp;
			if (!fu_cbor_item_get_integer(item_value, &tmp, error))
				return FALSE;
			if (tmp < 0) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "payload size cannot be negative");
				return FALSE;
			}
			payload->size = (guint64)tmp;
		} else if (tag_id == FU_COSWID_TAG_HASH) {
			if (fu_cbor_item_get_kind(item_value) == FU_CBOR_ITEM_KIND_ARRAY &&
			    fu_cbor_item_array_length(item_value) >= 1) {
				FuCborItem *value = fu_cbor_item_array_index(item_value, 0);
				/* we can't use fu_coswid_parse_one_or_many() here as
				 * the hash is an array, not a map -- for some reason */
				if (fu_cbor_item_get_kind(value) == FU_CBOR_ITEM_KIND_ARRAY) {
					if (!fu_coswid_firmware_parse_hash_array(item_value,
										 payload,
										 error))
						return FALSE;
				} else {
					if (!fu_coswid_firmware_parse_hash(item_value,
									   payload,
									   error))
						return FALSE;
				}
			} else {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
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

/* @userdata: a #FuCoswidFirmware */
static gboolean
fu_coswid_firmware_parse_path_elements(FuCborItem *item, gpointer user_data, GError **error)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(user_data);
	for (gsize i = 0; i < fu_cbor_item_map_length(item); i++) {
		FuCoswidTag tag_id = 0;
		FuCborItem *item_key = NULL;
		FuCborItem *item_value = NULL;

		fu_cbor_item_map_index(item, i, &item_key, &item_value);
		if (!fu_coswid_read_tag(item_key, &tag_id, error)) {
			g_prefix_error(error, "failed to parse elements tag %u: ", (guint)i);
			return FALSE;
		}
		if (tag_id == FU_COSWID_TAG_FILE) {
			if (!fu_coswid_parse_one_or_many(item_value,
							 fu_coswid_firmware_parse_file,
							 self, /* user_data */
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

/* @userdata: a #FuCoswidFirmware */
static gboolean
fu_coswid_firmware_parse_directory(FuCborItem *item, gpointer user_data, GError **error)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(user_data);
	for (gsize i = 0; i < fu_cbor_item_map_length(item); i++) {
		FuCoswidTag tag_id = 0;
		FuCborItem *item_key = NULL;
		FuCborItem *item_value = NULL;

		fu_cbor_item_map_index(item, i, &item_key, &item_value);
		if (!fu_coswid_read_tag(item_key, &tag_id, error)) {
			g_prefix_error(error, "failed to parse directory tag %u: ", (guint)i);
			return FALSE;
		}
		if (tag_id == FU_COSWID_TAG_PATH_ELEMENTS) {
			if (!fu_coswid_parse_one_or_many(item_value,
							 fu_coswid_firmware_parse_path_elements,
							 self, /* user_data */
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

/* @userdata: a #FuCoswidFirmware */
static gboolean
fu_coswid_firmware_parse_payload(FuCborItem *item, gpointer user_data, GError **error)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(user_data);
	for (gsize i = 0; i < fu_cbor_item_map_length(item); i++) {
		FuCoswidTag tag_id = 0;
		FuCborItem *item_key = NULL;
		FuCborItem *item_value = NULL;

		fu_cbor_item_map_index(item, i, &item_key, &item_value);
		if (!fu_coswid_read_tag(item_key, &tag_id, error)) {
			g_prefix_error(error, "failed to parse payload tag %u: ", (guint)i);
			return FALSE;
		}
		if (tag_id == FU_COSWID_TAG_FILE) {
			if (!fu_coswid_parse_one_or_many(item_value,
							 fu_coswid_firmware_parse_file,
							 self, /* user_data */
							 error))
				return FALSE;
		} else if (tag_id == FU_COSWID_TAG_DIRECTORY) {
			if (!fu_coswid_parse_one_or_many(item_value,
							 fu_coswid_firmware_parse_directory,
							 self, /* user_data */
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
fu_coswid_firmware_parse_entity_name(FuCoswidFirmwareEntity *entity,
				     FuCborItem *item,
				     GError **error)
{
	/* we might be calling this twice... */
	g_free(entity->name);

	entity->name = fu_coswid_read_string(item, error);
	if (entity->name == NULL) {
		g_prefix_error_literal(error, "failed to parse entity name: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_coswid_firmware_parse_entity_regid(FuCoswidFirmwareEntity *entity,
				      FuCborItem *item,
				      GError **error)
{
	/* we might be calling this twice... */
	g_free(entity->regid);

	entity->regid = fu_coswid_read_string(item, error);
	if (entity->regid == NULL) {
		g_prefix_error_literal(error, "failed to parse entity regid: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_coswid_firmware_parse_entity_role(FuCoswidFirmwareEntity *entity,
				     FuCborItem *item,
				     GError **error)
{
	if (fu_cbor_item_get_kind(item) == FU_CBOR_ITEM_KIND_INTEGER) {
		gint64 tmp = 0;
		if (!fu_cbor_item_get_integer(item, &tmp, error)) {
			g_prefix_error_literal(error, "failed to parse entity role: ");
			return FALSE;
		}
		if (tmp <= FU_COSWID_ENTITY_ROLE_UNKNOWN || tmp >= FU_COSWID_ENTITY_ROLE_LAST) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid entity role 0x%x",
				    (guint)tmp);
			return FALSE;
		}
		FU_BIT_SET(entity->roles, tmp);

	} else if (fu_cbor_item_get_kind(item) == FU_CBOR_ITEM_KIND_ARRAY) {
		for (guint j = 0; j < fu_cbor_item_array_length(item); j++) {
			gint64 tmp = 0;
			FuCborItem *value = fu_cbor_item_array_index(item, j);
			if (!fu_cbor_item_get_integer(value, &tmp, error)) {
				g_prefix_error_literal(error, "failed to parse entity role: ");
				return FALSE;
			}
			if (tmp <= FU_COSWID_ENTITY_ROLE_UNKNOWN ||
			    tmp >= FU_COSWID_ENTITY_ROLE_LAST) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid entity role 0x%x",
					    (guint)tmp);
				return FALSE;
			}
			FU_BIT_SET(entity->roles, tmp);
		}
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "entity role item is not an uint or array");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* @userdata: a #FuCoswidFirmware */
static gboolean
fu_coswid_firmware_parse_entity(FuCborItem *item, gpointer user_data, GError **error)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(user_data);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuCoswidFirmwareEntity) entity = g_new0(FuCoswidFirmwareEntity, 1);

	for (gsize i = 0; i < fu_cbor_item_map_length(item); i++) {
		FuCoswidTag tag_id = 0;
		FuCborItem *item_key = NULL;
		FuCborItem *item_value = NULL;

		fu_cbor_item_map_index(item, i, &item_key, &item_value);
		if (!fu_coswid_read_tag(item_key, &tag_id, error)) {
			g_prefix_error(error, "failed to parse entity tag %u: ", (guint)i);
			return FALSE;
		}
		if (tag_id == FU_COSWID_TAG_ENTITY_NAME) {
			if (!fu_coswid_firmware_parse_entity_name(entity, item_value, error))
				return FALSE;
		} else if (tag_id == FU_COSWID_TAG_REG_ID) {
			if (!fu_coswid_firmware_parse_entity_regid(entity, item_value, error))
				return FALSE;
		} else if (tag_id == FU_COSWID_TAG_ROLE) {
			if (!fu_coswid_firmware_parse_entity_role(entity, item_value, error))
				return FALSE;
		} else {
			g_debug("unhandled tag %s from %s",
				fu_coswid_tag_to_string(tag_id),
				fu_coswid_tag_to_string(FU_COSWID_TAG_ENTITY));
		}
	}

	/* sanity check */
	if (entity->name == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "entity does not have a name");
		return FALSE;
	}
	if (entity->roles == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "entity has no roles");
		return FALSE;
	}

	/* success */
	g_ptr_array_add(priv->entities, g_steal_pointer(&entity));
	return TRUE;
}

static gboolean
fu_coswid_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(firmware);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize offset = 0;
	g_autoptr(FuCborItem) item = NULL;

	item = fu_cbor_parse(stream,
			     &offset,
			     FU_COSWID_CBOR_MAX_DEPTH,
			     FU_COSWID_CBOR_MAX_ITEMS,
			     FU_COSWID_CBOR_MAX_LENGTH,
			     error);
	if (item == NULL)
		return FALSE;
	fu_firmware_set_size(firmware, offset);

	/* pretty-print the result */
	if (g_getenv("FWUPD_CBOR_VERBOSE") != NULL) {
		g_autofree gchar *str = fu_cbor_item_to_string(item);
		g_debug("%s", str);
	}

	/* sanity check */
	if (fu_cbor_item_get_kind(item) != FU_CBOR_ITEM_KIND_MAP) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "root item is not a map");
		return FALSE;
	}

	/* parse out anything interesting */
	for (gsize i = 0; i < fu_cbor_item_map_length(item); i++) {
		FuCoswidTag tag_id = 0;
		FuCborItem *item_key = NULL;
		FuCborItem *item_value = NULL;

		fu_cbor_item_map_index(item, i, &item_key, &item_value);
		if (!fu_coswid_read_tag(item_key, &tag_id, error)) {
			g_prefix_error(error, "failed to parse root tag %u: ", (guint)i);
			return FALSE;
		}

		/* identity can be specified as a string or in binary */
		if (tag_id == FU_COSWID_TAG_TAG_ID) {
			g_autofree gchar *str = fu_coswid_read_string(item_value, error);
			if (str == NULL) {
				g_prefix_error_literal(error, "failed to parse tag-id: ");
				return FALSE;
			}
			fu_firmware_set_id(firmware, str);
		} else if (tag_id == FU_COSWID_TAG_SOFTWARE_NAME) {
			g_free(priv->product);
			priv->product = fu_coswid_read_string(item_value, error);
			if (priv->product == NULL) {
				g_prefix_error_literal(error, "failed to parse product: ");
				return FALSE;
			}
		} else if (tag_id == FU_COSWID_TAG_SOFTWARE_VERSION) {
			g_autofree gchar *str = fu_coswid_read_string(item_value, error);
			if (str == NULL) {
				g_prefix_error_literal(error, "failed to parse software-version: ");
				return FALSE;
			}
			fu_firmware_set_version(firmware, str);
		} else if (tag_id == FU_COSWID_TAG_VERSION_SCHEME) {
			if (!fu_coswid_read_version_scheme(item_value,
							   &priv->version_scheme,
							   error))
				return FALSE;
		} else if (tag_id == FU_COSWID_TAG_SOFTWARE_META) {
			if (!fu_coswid_parse_one_or_many(item_value,
							 fu_coswid_firmware_parse_meta,
							 self, /* user_data */
							 error))
				return FALSE;
		} else if (tag_id == FU_COSWID_TAG_EVIDENCE) {
			if (!fu_coswid_parse_one_or_many(item_value,
							 fu_coswid_firmware_parse_evidence,
							 self, /* user_data */
							 error))
				return FALSE;
		} else if (tag_id == FU_COSWID_TAG_LINK) {
			if (!fu_coswid_parse_one_or_many(item_value,
							 fu_coswid_firmware_parse_link,
							 self, /* user_data */
							 error))
				return FALSE;
		} else if (tag_id == FU_COSWID_TAG_PAYLOAD) {
			if (!fu_coswid_parse_one_or_many(item_value,
							 fu_coswid_firmware_parse_payload,
							 self, /* user_data */
							 error))
				return FALSE;
		} else if (tag_id == FU_COSWID_TAG_ENTITY) {
			if (!fu_coswid_parse_one_or_many(item_value,
							 fu_coswid_firmware_parse_entity,
							 self, /* user_data */
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
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not enough SBOM data");
		return FALSE;
	}

	/* success */
	return TRUE;
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
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
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
				return fu_bytes_to_string(hash->value);
		}
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "no hash kind %s",
		    fwupd_checksum_type_to_string_display(csum_kind));
	return NULL;
}

/**
 * fu_coswid_firmware_get_product:
 * @self: a #FuCoswidFirmware
 *
 * Gets the product name.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 2.0.12
 **/
const gchar *
fu_coswid_firmware_get_product(FuCoswidFirmware *self)
{
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_COSWID_FIRMWARE(self), NULL);
	return priv->product;
}

/**
 * fu_coswid_firmware_get_persistent_id:
 * @self: a #FuCoswidFirmware
 *
 * Gets the persistent ID.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 2.0.17
 **/
const gchar *
fu_coswid_firmware_get_persistent_id(FuCoswidFirmware *self)
{
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_COSWID_FIRMWARE(self), NULL);
	return priv->persistent_id;
}

/**
 * fu_coswid_firmware_get_device_id:
 * @self: a #FuCoswidFirmware
 *
 * Gets the device ID.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 2.0.17
 **/
const gchar *
fu_coswid_firmware_get_device_id(FuCoswidFirmware *self)
{
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_COSWID_FIRMWARE(self), NULL);
	return priv->device_id;
}

static gboolean
fu_coswid_firmware_write_hash(FuCborItem *root, FuCoswidFirmwareHash *hash, GError **error)
{
	g_autoptr(FuCborItem) item_hash = fu_cbor_item_new_array();
	g_autoptr(FuCborItem) item_hash_alg_id = fu_cbor_item_new_integer(hash->alg_id);
	g_autoptr(FuCborItem) item_hash_value = fu_cbor_item_new_bytes(hash->value);
	if (!fu_cbor_item_array_append(item_hash, item_hash_alg_id, error))
		return FALSE;
	if (!fu_cbor_item_array_append(item_hash, item_hash_value, error))
		return FALSE;
	if (!fu_cbor_item_array_append(root, item_hash, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_coswid_firmware_write_payload(FuCborItem *root, FuCoswidFirmwarePayload *payload, GError **error)
{
	g_autoptr(FuCborItem) item_payload = fu_cbor_item_new_map();
	g_autoptr(FuCborItem) item_file = fu_cbor_item_new_map();
	if (payload->name != NULL)
		fu_coswid_write_tag_string(item_file, FU_COSWID_TAG_FS_NAME, payload->name);
	if (payload->size != 0)
		fu_coswid_write_tag_integer(item_file, FU_COSWID_TAG_SIZE, payload->size);
	if (payload->hashes->len > 0) {
		g_autoptr(FuCborItem) item_hashes = fu_cbor_item_new_array();
		for (guint j = 0; j < payload->hashes->len; j++) {
			FuCoswidFirmwareHash *hash = g_ptr_array_index(payload->hashes, j);
			if (!fu_coswid_firmware_write_hash(item_hashes, hash, error)) {
				g_prefix_error_literal(error, "failed to add payload: ");
				return FALSE;
			}
		}
		fu_coswid_write_tag_item(item_file, FU_COSWID_TAG_HASH, item_hashes);
	}
	fu_coswid_write_tag_item(item_payload, FU_COSWID_TAG_FILE, item_file);
	return fu_cbor_item_array_append(root, item_payload, error);
}

static gboolean
fu_coswid_firmware_write_entity(FuCborItem *root, FuCoswidFirmwareEntity *entity, GError **error)
{
	g_autoptr(FuCborItem) item_entity = fu_cbor_item_new_map();
	g_autoptr(FuCborItem) item_roles = fu_cbor_item_new_array();
	if (entity->name != NULL)
		fu_coswid_write_tag_string(item_entity, FU_COSWID_TAG_ENTITY_NAME, entity->name);
	if (entity->regid != NULL)
		fu_coswid_write_tag_string(item_entity, FU_COSWID_TAG_REG_ID, entity->regid);
	for (guint j = 0; j < FU_COSWID_ENTITY_ROLE_LAST; j++) {
		if (FU_BIT_IS_SET(entity->roles, j)) {
			g_autoptr(FuCborItem) item_role = fu_cbor_item_new_integer(j);
			if (!fu_cbor_item_array_append(item_roles, item_role, error))
				return FALSE;
		}
	}
	fu_coswid_write_tag_item(item_entity, FU_COSWID_TAG_ROLE, item_roles);
	return fu_cbor_item_array_append(root, item_entity, error);
}

static gboolean
fu_coswid_firmware_write_evidence(FuCborItem *root, const gchar *device_id, GError **error)
{
	g_autoptr(FuCborItem) item_entity = fu_cbor_item_new_map();
	fu_coswid_write_tag_string(item_entity, FU_COSWID_TAG_DEVICE_ID, device_id);
	return fu_cbor_item_array_append(root, item_entity, error);
}

static GByteArray *
fu_coswid_firmware_write(FuFirmware *firmware, GError **error)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(firmware);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuCborItem) root = fu_cbor_item_new_map();
	g_autoptr(FuCborItem) item_meta = fu_cbor_item_new_map();

	/* preallocate the map structure */
	fu_coswid_write_tag_string(root, FU_COSWID_TAG_LANG, "en-US");
	if (fu_firmware_get_id(firmware) != NULL) {
		fwupd_guid_t uuid = {0};
		if (fwupd_guid_from_string(fu_firmware_get_id(firmware),
					   &uuid,
					   FWUPD_GUID_FLAG_NONE,
					   NULL)) {
			fu_coswid_write_tag_bytestring(root,
						       FU_COSWID_TAG_TAG_ID,
						       (const guint8 *)&uuid,
						       sizeof(uuid));
		} else {
			fu_coswid_write_tag_string(root,
						   FU_COSWID_TAG_TAG_ID,
						   fu_firmware_get_id(firmware));
		}
	}
	fu_coswid_write_tag_bool(root, FU_COSWID_TAG_CORPUS, TRUE);
	if (priv->product != NULL)
		fu_coswid_write_tag_string(root, FU_COSWID_TAG_SOFTWARE_NAME, priv->product);
	if (fu_firmware_get_version(firmware) != NULL) {
		fu_coswid_write_tag_string(root,
					   FU_COSWID_TAG_SOFTWARE_VERSION,
					   fu_firmware_get_version(firmware));
	}
	if (priv->version_scheme != FU_COSWID_VERSION_SCHEME_UNKNOWN)
		fu_coswid_write_tag_integer(root,
					    FU_COSWID_TAG_VERSION_SCHEME,
					    priv->version_scheme);
	fu_coswid_write_tag_item(root, FU_COSWID_TAG_SOFTWARE_META, item_meta);
	fu_coswid_write_tag_string(item_meta, FU_COSWID_TAG_GENERATOR, PACKAGE_NAME);
	if (priv->summary != NULL)
		fu_coswid_write_tag_string(item_meta, FU_COSWID_TAG_SUMMARY, priv->summary);
	if (priv->colloquial_version != NULL) {
		fu_coswid_write_tag_string(item_meta,
					   FU_COSWID_TAG_COLLOQUIAL_VERSION,
					   priv->colloquial_version);
	}
	if (priv->persistent_id != NULL) {
		fu_coswid_write_tag_string(item_meta,
					   FU_COSWID_TAG_PERSISTENT_ID,
					   priv->persistent_id);
	}

	/* add evidence */
	if (priv->device_id != NULL) {
		g_autoptr(FuCborItem) items = fu_cbor_item_new_array();
		if (!fu_coswid_firmware_write_evidence(items, priv->device_id, error))
			return NULL;
		fu_coswid_write_tag_item(root, FU_COSWID_TAG_EVIDENCE, items);
	}

	/* add entities */
	if (priv->entities->len > 0) {
		g_autoptr(FuCborItem) item_entities = fu_cbor_item_new_array();
		for (guint i = 0; i < priv->entities->len; i++) {
			FuCoswidFirmwareEntity *entity = g_ptr_array_index(priv->entities, i);
			if (!fu_coswid_firmware_write_entity(item_entities, entity, error))
				return NULL;
		}
		fu_coswid_write_tag_item(root, FU_COSWID_TAG_ENTITY, item_entities);
	}

	/* add links */
	if (priv->links->len > 0) {
		g_autoptr(FuCborItem) item_links = fu_cbor_item_new_array();
		for (guint i = 0; i < priv->links->len; i++) {
			FuCoswidFirmwareLink *link = g_ptr_array_index(priv->links, i);
			g_autoptr(FuCborItem) item_link = fu_cbor_item_new_map();
			if (link->href != NULL) {
				fu_coswid_write_tag_string(item_link,
							   FU_COSWID_TAG_HREF,
							   link->href);
			}
			fu_coswid_write_tag_integer(item_link, FU_COSWID_TAG_REL, link->rel);
			if (!fu_cbor_item_array_append(item_links, item_link, error))
				return NULL;
		}
		fu_coswid_write_tag_item(root, FU_COSWID_TAG_LINK, item_links);
	}

	/* add payloads */
	if (priv->payloads->len > 0) {
		g_autoptr(FuCborItem) item_payloads = fu_cbor_item_new_array();
		for (guint i = 0; i < priv->payloads->len; i++) {
			FuCoswidFirmwarePayload *payload = g_ptr_array_index(priv->payloads, i);
			if (!fu_coswid_firmware_write_payload(item_payloads, payload, error))
				return NULL;
		}
		fu_coswid_write_tag_item(root, FU_COSWID_TAG_PAYLOAD, item_payloads);
	}

	/* serialize */
	return fu_cbor_item_write(root, error);
}

static gboolean
fu_coswid_firmware_build_entity(FuCoswidFirmware *self, XbNode *n, GError **error)
{
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	FuCoswidEntityRole role;
	g_autoptr(GPtrArray) roles = NULL;
	g_autoptr(FuCoswidFirmwareEntity) entity = g_new0(FuCoswidFirmwareEntity, 1);

	/* these are required */
	tmp = xb_node_query_text(n, "name", error);
	if (tmp == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}
	entity->name = g_strdup(tmp);
	tmp = xb_node_query_text(n, "regid", error);
	if (tmp == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}
	entity->regid = g_strdup(tmp);

	/* optional */
	roles = xb_node_query(n, "role", 0, NULL);
	if (roles != NULL) {
		for (guint i = 0; i < roles->len; i++) {
			XbNode *c = g_ptr_array_index(roles, i);
			tmp = xb_node_get_text(c);
			role = fu_coswid_entity_role_from_string(tmp);
			if (role == FU_COSWID_ENTITY_ROLE_UNKNOWN ||
			    role >= FU_COSWID_ENTITY_ROLE_LAST) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "failed to parse entity role %s",
					    tmp);
				return FALSE;
			}
			FU_BIT_SET(entity->roles, role);
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
	if (tmp == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}
	link->href = g_strdup(tmp);

	/* optional */
	tmp = xb_node_query_text(n, "rel", NULL);
	if (tmp != NULL) {
		link->rel = fu_coswid_link_rel_from_string(tmp);
		if (link->rel == FU_COSWID_LINK_REL_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
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
	if (tmp == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}
	hash->value = fu_bytes_from_string(tmp, error);
	if (hash->value == NULL)
		return FALSE;

	/* optional */
	tmp = xb_node_query_text(n, "alg_id", NULL);
	if (tmp != NULL) {
		hash->alg_id = fu_coswid_hash_alg_from_string(tmp);
		if (hash->alg_id == FU_COSWID_HASH_ALG_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
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
	tmp = xb_node_query_text(n, "persistent_id", NULL);
	if (tmp != NULL)
		priv->persistent_id = g_strdup(tmp);
	tmp = xb_node_query_text(n, "device_id", NULL);
	if (tmp != NULL)
		priv->device_id = g_strdup(tmp);

	tmp = xb_node_query_text(n, "version_scheme", NULL);
	if (tmp != NULL) {
		priv->version_scheme = fu_coswid_version_scheme_from_string(tmp);
		if (priv->version_scheme == FU_COSWID_VERSION_SCHEME_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
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
	fu_xmlb_builder_insert_kv(bn, "persistent_id", priv->persistent_id);
	fu_xmlb_builder_insert_kv(bn, "device_id", priv->device_id);
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
			g_autofree gchar *value = fu_bytes_to_string(hash->value);
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
		for (guint j = 0; j < FU_COSWID_ENTITY_ROLE_LAST; j++) {
			if (FU_BIT_IS_SET(entity->roles, j)) {
				fu_xmlb_builder_insert_kv(bc,
							  "role",
							  fu_coswid_entity_role_to_string(j));
			}
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
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_ALLOW_LINEAR);
	fu_firmware_set_size_max(FU_FIRMWARE(self), 1 * FU_MB);
}

static void
fu_coswid_firmware_finalize(GObject *object)
{
	FuCoswidFirmware *self = FU_COSWID_FIRMWARE(object);
	FuCoswidFirmwarePrivate *priv = GET_PRIVATE(self);

	g_free(priv->product);
	g_free(priv->summary);
	g_free(priv->colloquial_version);
	g_free(priv->persistent_id);
	g_free(priv->device_id);
	g_ptr_array_unref(priv->links);
	g_ptr_array_unref(priv->payloads);
	g_ptr_array_unref(priv->entities);

	G_OBJECT_CLASS(fu_coswid_firmware_parent_class)->finalize(object);
}

static void
fu_coswid_firmware_class_init(FuCoswidFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_coswid_firmware_finalize;
	firmware_class->parse = fu_coswid_firmware_parse;
	firmware_class->write = fu_coswid_firmware_write;
	firmware_class->build = fu_coswid_firmware_build;
	firmware_class->export = fu_coswid_firmware_export;
	firmware_class->get_checksum = fu_coswid_firmware_get_checksum;
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
