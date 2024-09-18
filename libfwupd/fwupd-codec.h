/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include "fwupd-build.h"

#define FWUPD_TYPE_CODEC (fwupd_codec_get_type())
G_DECLARE_INTERFACE(FwupdCodec, fwupd_codec, FWUPD, CODEC, GObject)

/**
 * FwupdCodecFlags:
 *
 * The flags to use when converting data from one form to another.
 **/
typedef enum {
	/**
	 * FWUPD_CODEC_FLAG_NONE:
	 *
	 * No flags set.
	 *
	 * Since: 2.0.0
	 */
	FWUPD_CODEC_FLAG_NONE = 0,
	/**
	 * FWUPD_CODEC_FLAG_TRUSTED:
	 *
	 * Include values that may be regarded as trusted or sensitive.
	 *
	 * Since: 2.0.0
	 */
	FWUPD_CODEC_FLAG_TRUSTED = 1 << 0,
} FwupdCodecFlags;

struct _FwupdCodecInterface {
	GTypeInterface g_iface;
	void (*add_string)(FwupdCodec *self, guint idt, GString *str);
	gchar *(*to_string)(FwupdCodec *self);
	void (*add_json)(FwupdCodec *self, JsonBuilder *builder, FwupdCodecFlags flags);
	gboolean (*from_json)(FwupdCodec *self, JsonNode *json_node, GError **error);
	void (*add_variant)(FwupdCodec *self, GVariantBuilder *builder, FwupdCodecFlags flags);
	GVariant *(*to_variant)(FwupdCodec *self, FwupdCodecFlags flags);
	gboolean (*from_variant)(FwupdCodec *self, GVariant *value, GError **error);
	void (*from_variant_iter)(FwupdCodec *self, GVariantIter *iter);
	/*< private >*/
	void (*_fwupd_reserved1)(void);
	void (*_fwupd_reserved2)(void);
	void (*_fwupd_reserved3)(void);
	void (*_fwupd_reserved4)(void);
	void (*_fwupd_reserved5)(void);
	void (*_fwupd_reserved6)(void);
	void (*_fwupd_reserved7)(void);
};

gchar *
fwupd_codec_to_string(FwupdCodec *self) G_GNUC_NON_NULL(1);
void
fwupd_codec_add_string(FwupdCodec *self, guint idt, GString *str) G_GNUC_NON_NULL(1, 3);
gboolean
fwupd_codec_from_json(FwupdCodec *self, JsonNode *json_node, GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_codec_from_json_string(FwupdCodec *self, const gchar *json, GError **error)
    G_GNUC_NON_NULL(1, 2);
void
fwupd_codec_to_json(FwupdCodec *self, JsonBuilder *builder, FwupdCodecFlags flags)
    G_GNUC_NON_NULL(1, 2);
gchar *
fwupd_codec_to_json_string(FwupdCodec *self, FwupdCodecFlags flags, GError **error);

void
fwupd_codec_array_to_json(GPtrArray *array,
			  const gchar *member_name,
			  JsonBuilder *builder,
			  FwupdCodecFlags flags);

GVariant *
fwupd_codec_to_variant(FwupdCodec *self, FwupdCodecFlags flags) G_GNUC_NON_NULL(1);
gboolean
fwupd_codec_from_variant(FwupdCodec *self, GVariant *value, GError **error) G_GNUC_NON_NULL(1, 2);

GVariant *
fwupd_codec_array_to_variant(GPtrArray *array, FwupdCodecFlags flags) G_GNUC_NON_NULL(1);
GPtrArray *
fwupd_codec_array_from_variant(GVariant *value, GType gtype, GError **error) G_GNUC_NON_NULL(1);

void
fwupd_codec_string_append(GString *str, guint idt, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 3);
void
fwupd_codec_string_append_int(GString *str, guint idt, const gchar *key, guint64 value)
    G_GNUC_NON_NULL(1, 3);
void
fwupd_codec_string_append_hex(GString *str, guint idt, const gchar *key, guint64 value)
    G_GNUC_NON_NULL(1, 3);
void
fwupd_codec_string_append_bool(GString *str, guint idt, const gchar *key, gboolean value)
    G_GNUC_NON_NULL(1, 3);
void
fwupd_codec_string_append_time(GString *str, guint idt, const gchar *key, guint64 value)
    G_GNUC_NON_NULL(1, 3);
void
fwupd_codec_string_append_size(GString *str, guint idt, const gchar *key, guint64 value)
    G_GNUC_NON_NULL(1, 3);

void
fwupd_codec_json_append(JsonBuilder *builder, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2);
void
fwupd_codec_json_append_strv(JsonBuilder *builder, const gchar *key, gchar **value)
    G_GNUC_NON_NULL(1, 2);
void
fwupd_codec_json_append_int(JsonBuilder *builder, const gchar *key, guint64 value)
    G_GNUC_NON_NULL(1, 2);
void
fwupd_codec_json_append_bool(JsonBuilder *builder, const gchar *key, gboolean value)
    G_GNUC_NON_NULL(1, 2);
