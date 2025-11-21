/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuClient"

#include "config.h"

#include "fu-client.h"

struct _FuClient {
	GObject parent_instance;
	gchar *sender;
	GHashTable *hints; /* str:str */
	FwupdFeatureFlags feature_flags;
	FuClientFlags flags;
};

G_DEFINE_TYPE(FuClient, fu_client, G_TYPE_OBJECT)

enum { PROP_0, PROP_SENDER, PROP_FLAGS, PROP_LAST };

void
fu_client_set_feature_flags(FuClient *self, FwupdFeatureFlags feature_flags)
{
	g_return_if_fail(FU_IS_CLIENT(self));
	self->feature_flags = feature_flags;
}

FwupdFeatureFlags
fu_client_get_feature_flags(FuClient *self)
{
	g_return_val_if_fail(FU_IS_CLIENT(self), FWUPD_FEATURE_FLAG_NONE);
	return self->feature_flags;
}

const gchar *
fu_client_get_sender(FuClient *self)
{
	g_return_val_if_fail(FU_IS_CLIENT(self), NULL);
	return self->sender;
}

const gchar *
fu_client_lookup_hint(FuClient *self, const gchar *key)
{
	g_return_val_if_fail(FU_IS_CLIENT(self), NULL);
	g_return_val_if_fail(key != NULL, NULL);
	return g_hash_table_lookup(self->hints, key);
}

void
fu_client_insert_hint(FuClient *self, const gchar *key, const gchar *value)
{
	g_return_if_fail(FU_IS_CLIENT(self));
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);
	g_hash_table_insert(self->hints, g_strdup(key), g_strdup(value));
}

static void
fu_client_add_flag(FuClient *self, FuClientFlags flag)
{
	g_return_if_fail(FU_IS_CLIENT(self));
	g_return_if_fail(flag != FU_CLIENT_FLAG_NONE);
	if ((self->flags & flag) > 0)
		return;
	self->flags |= flag;
	g_object_notify(G_OBJECT(self), "flags");
}

void
fu_client_remove_flag(FuClient *self, FuClientFlags flag)
{
	g_return_if_fail(FU_IS_CLIENT(self));
	g_return_if_fail(flag != FU_CLIENT_FLAG_NONE);
	if ((self->flags & flag) == 0)
		return;
	self->flags &= ~flag;
	g_object_notify(G_OBJECT(self), "flags");
}

gboolean
fu_client_has_flag(FuClient *self, FuClientFlags flag)
{
	g_return_val_if_fail(FU_IS_CLIENT(self), FALSE);
	g_return_val_if_fail(flag != FU_CLIENT_FLAG_NONE, FALSE);
	return (self->flags & flag) > 0;
}

static void
fu_client_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuClient *self = FU_CLIENT(object);
	switch (prop_id) {
	case PROP_SENDER:
		g_value_set_string(value, self->sender);
		break;
	case PROP_FLAGS:
		g_value_set_uint64(value, self->flags);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_client_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuClient *self = FU_CLIENT(object);
	switch (prop_id) {
	case PROP_SENDER:
		self->sender = g_value_dup_string(value);
		break;
	case PROP_FLAGS:
		fu_client_add_flag(self, (FuClientFlags)g_value_get_uint64(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_client_init(FuClient *self)
{
	self->hints = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

static void
fu_client_finalize(GObject *obj)
{
	FuClient *self = FU_CLIENT(obj);
	g_free(self->sender);
	g_hash_table_unref(self->hints);
	G_OBJECT_CLASS(fu_client_parent_class)->finalize(obj);
}

static void
fu_client_class_init(FuClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fu_client_finalize;
	object_class->get_property = fu_client_get_property;
	object_class->set_property = fu_client_set_property;

	pspec = g_param_spec_string("sender",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_SENDER, pspec);

	pspec = g_param_spec_uint64("flags",
				    NULL,
				    NULL,
				    0,
				    G_MAXUINT64,
				    0,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FLAGS, pspec);
}

FuClient *
fu_client_new(const gchar *sender)
{
	return FU_CLIENT(g_object_new(FU_TYPE_CLIENT,
				      "sender",
				      sender,
				      "flags",
				      (guint64)FU_CLIENT_FLAG_ACTIVE,
				      NULL));
}
