/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuKeyring"

#include "config.h"

#include "fwupd-error.h"

#include "fu-keyring-result.h"

struct _FuKeyringResult
{
	GObject			 parent_instance;
	gint64			 timestamp;
	gchar			*authority;
};

G_DEFINE_TYPE (FuKeyringResult, fu_keyring_result, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_TIMESTAMP,
	PROP_AUTHORITY,
	PROP_LAST
};

gint64
fu_keyring_result_get_timestamp (FuKeyringResult *self)
{
	g_return_val_if_fail (FU_IS_KEYRING_RESULT (self), 0);
	return self->timestamp;
}

const gchar *
fu_keyring_result_get_authority (FuKeyringResult *self)
{
	g_return_val_if_fail (FU_IS_KEYRING_RESULT (self), NULL);
	return self->authority;
}

static void
fu_keyring_result_finalize (GObject *object)
{
	FuKeyringResult *self = FU_KEYRING_RESULT (object);
	g_free (self->authority);
	G_OBJECT_CLASS (fu_keyring_result_parent_class)->finalize (object);
}

static void
fu_keyring_result_get_property (GObject *object, guint prop_id,
				GValue *value, GParamSpec *pspec)
{
	FuKeyringResult *self = FU_KEYRING_RESULT (object);
	switch (prop_id) {
	case PROP_TIMESTAMP:
		g_value_set_int64 (value, self->timestamp);
		break;
	case PROP_AUTHORITY:
		g_value_set_string (value, self->authority);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_keyring_result_set_property (GObject *object, guint prop_id,
				const GValue *value, GParamSpec *pspec)
{
	FuKeyringResult *self = FU_KEYRING_RESULT (object);
	switch (prop_id) {
	case PROP_TIMESTAMP:
		self->timestamp = g_value_get_int64 (value);
		break;
	case PROP_AUTHORITY:
		self->authority = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_keyring_result_class_init (FuKeyringResultClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;

	object_class->get_property = fu_keyring_result_get_property;
	object_class->set_property = fu_keyring_result_set_property;
	object_class->finalize = fu_keyring_result_finalize;

	pspec = g_param_spec_int64 ("timestamp", NULL, NULL,
				    0, G_MAXINT64, 0,
				    G_PARAM_READWRITE |
				    G_PARAM_CONSTRUCT_ONLY |
				    G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_TIMESTAMP, pspec);

	pspec = g_param_spec_string ("authority", NULL, NULL, NULL,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_AUTHORITY, pspec);
}

static void
fu_keyring_result_init (FuKeyringResult *self)
{
}
