/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuDummyEfivars"

#include "config.h"

#include "fu-dummy-efivars.h"

typedef struct {
	gchar *guid;
	gchar *name;
	guint32 attr;
	GByteArray *buf;
} FuDummyEfivarsKey;

struct _FuDummyEfivars {
	FuEfivars parent_instance;
	GPtrArray *keys; /* of FuDummyEfivarsKey */
};

G_DEFINE_TYPE(FuDummyEfivars, fu_dummy_efivars, FU_TYPE_EFIVARS)

static void
fu_dummy_efivars_key_free(FuDummyEfivarsKey *key)
{
	g_free(key->guid);
	g_free(key->name);
	g_byte_array_unref(key->buf);
	g_free(key);
}

static gboolean
fu_dummy_efivars_supported(FuEfivars *efivars, GError **error)
{
	return TRUE;
}

static FuDummyEfivarsKey *
fu_dummy_efivars_find_by_guid_name(FuDummyEfivars *self, const gchar *guid, const gchar *name)
{
	for (guint i = 0; i < self->keys->len; i++) {
		FuDummyEfivarsKey *key = g_ptr_array_index(self->keys, i);
		if (g_strcmp0(guid, key->guid) == 0 && g_strcmp0(name, key->name) == 0)
			return key;
	}
	return NULL;
}

static gboolean
fu_dummy_efivars_delete(FuEfivars *efivars, const gchar *guid, const gchar *name, GError **error)
{
	FuDummyEfivars *self = FU_DUMMY_EFIVARS(efivars);
	FuDummyEfivarsKey *key = fu_dummy_efivars_find_by_guid_name(self, guid, name);
	if (key == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no key to delete");
		return FALSE;
	}
	g_ptr_array_remove(self->keys, key);
	return TRUE;
}

static gboolean
fu_dummy_efivars_delete_with_glob(FuEfivars *efivars,
				  const gchar *guid,
				  const gchar *name_glob,
				  GError **error)
{
	FuDummyEfivars *self = FU_DUMMY_EFIVARS(efivars);
	g_autoptr(GPtrArray) keys_tmp = g_ptr_array_new();
	for (guint i = 0; i < self->keys->len; i++) {
		FuDummyEfivarsKey *key = g_ptr_array_index(self->keys, i);
		if (g_pattern_match_simple(name_glob, key->name))
			g_ptr_array_add(keys_tmp, key);
	}
	for (guint i = 0; i < keys_tmp->len; i++) {
		FuDummyEfivarsKey *key = g_ptr_array_index(keys_tmp, i);
		g_ptr_array_remove(self->keys, key);
	}
	return TRUE;
}

static gboolean
fu_dummy_efivars_exists_guid(FuDummyEfivars *self, const gchar *guid)
{
	for (guint i = 0; i < self->keys->len; i++) {
		FuDummyEfivarsKey *key = g_ptr_array_index(self->keys, i);
		if (g_strcmp0(guid, key->guid) == 0)
			return TRUE;
	}
	return FALSE;
}

static gboolean
fu_dummy_efivars_exists(FuEfivars *efivars, const gchar *guid, const gchar *name)
{
	FuDummyEfivars *self = FU_DUMMY_EFIVARS(efivars);
	if (name == NULL)
		return fu_dummy_efivars_exists_guid(self, guid);
	return fu_dummy_efivars_find_by_guid_name(self, guid, name) != NULL;
}

static gboolean
fu_dummy_efivars_get_data(FuEfivars *efivars,
			  const gchar *guid,
			  const gchar *name,
			  guint8 **data,
			  gsize *data_sz,
			  guint32 *attr,
			  GError **error)
{
	FuDummyEfivars *self = FU_DUMMY_EFIVARS(efivars);
	FuDummyEfivarsKey *key = fu_dummy_efivars_find_by_guid_name(self, guid, name);
	if (key == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "%s-%s not found",
			    guid,
			    name);
		return FALSE;
	}
	if (data != NULL)
		*data = g_memdup2(key->buf->data, key->buf->len);
	if (data_sz != NULL)
		*data_sz = key->buf->len;
	if (attr != NULL)
		*attr = key->attr;
	return TRUE;
}

static GPtrArray *
fu_dummy_efivars_get_names(FuEfivars *efivars, const gchar *guid, GError **error)
{
	FuDummyEfivars *self = FU_DUMMY_EFIVARS(efivars);
	g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func(g_free);
	for (guint i = 0; i < self->keys->len; i++) {
		FuDummyEfivarsKey *key = g_ptr_array_index(self->keys, i);
		if (g_strcmp0(guid, key->guid) == 0)
			g_ptr_array_add(names, g_strdup(key->name));
	}
	return g_steal_pointer(&names);
}

static guint64
fu_dummy_efivars_space_used(FuEfivars *efivars, GError **error)
{
	FuDummyEfivars *self = FU_DUMMY_EFIVARS(efivars);
	guint64 total = 0;
	for (guint i = 0; i < self->keys->len; i++) {
		FuDummyEfivarsKey *key = g_ptr_array_index(self->keys, i);
		total += 0x20 + strlen(key->name) + key->buf->len;
	}
	return total;
}

static gboolean
fu_dummy_efivars_set_data(FuEfivars *efivars,
			  const gchar *guid,
			  const gchar *name,
			  const guint8 *data,
			  gsize sz,
			  guint32 attr,
			  GError **error)
{
	FuDummyEfivars *self = FU_DUMMY_EFIVARS(efivars);
	FuDummyEfivarsKey *key = fu_dummy_efivars_find_by_guid_name(self, guid, name);
	if (key == NULL) {
		key = g_new0(FuDummyEfivarsKey, 1);
		key->guid = g_strdup(guid);
		key->name = g_strdup(name);
		key->buf = g_byte_array_new();
		g_ptr_array_add(self->keys, key);
	}
	key->attr = attr;
	g_byte_array_set_size(key->buf, 0);
	g_byte_array_append(key->buf, data, sz);
	return TRUE;
}

static void
fu_dummy_efivars_init(FuDummyEfivars *self)
{
	self->keys = g_ptr_array_new_with_free_func((GDestroyNotify)fu_dummy_efivars_key_free);
}

static void
fu_dummy_efivars_finalize(GObject *object)
{
	FuDummyEfivars *self = FU_DUMMY_EFIVARS(object);
	g_ptr_array_unref(self->keys);
	G_OBJECT_CLASS(fu_dummy_efivars_parent_class)->finalize(object);
}

static void
fu_dummy_efivars_class_init(FuDummyEfivarsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuEfivarsClass *efivars_class = FU_EFIVARS_CLASS(klass);
	efivars_class->supported = fu_dummy_efivars_supported;
	efivars_class->space_used = fu_dummy_efivars_space_used;
	efivars_class->exists = fu_dummy_efivars_exists;
	efivars_class->get_data = fu_dummy_efivars_get_data;
	efivars_class->set_data = fu_dummy_efivars_set_data;
	efivars_class->delete = fu_dummy_efivars_delete;
	efivars_class->delete_with_glob = fu_dummy_efivars_delete_with_glob;
	efivars_class->get_names = fu_dummy_efivars_get_names;
	object_class->finalize = fu_dummy_efivars_finalize;
}

FuEfivars *
fu_dummy_efivars_new(void)
{
	return FU_EFIVARS(g_object_new(FU_TYPE_DUMMY_EFIVARS, NULL));
}
