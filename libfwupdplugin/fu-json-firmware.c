/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-common.h"
#include "fu-json-firmware.h"

/**
 * FuJsonFirmware:
 *
 * A "dummy" loader that just checks if the file can be parsed and written as JSON format.
 */

typedef struct {
	FwupdJsonNode *json_node;
} FuJsonFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuJsonFirmware, fu_json_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_json_firmware_get_instance_private(o))

static gboolean
fu_json_firmware_parse(FuFirmware *firmware,
		       GInputStream *stream,
		       FuFirmwareParseFlags flags,
		       GError **error)
{
	FuJsonFirmware *self = FU_JSON_FIRMWARE(firmware);
	FuJsonFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();

#ifdef HAVE_FUZZER
	/* make the fuzzer spend time on complexity, not depth or length -> OOM */
	fwupd_json_parser_set_max_depth(json_parser, 5);
	fwupd_json_parser_set_max_items(json_parser, 10);
	fwupd_json_parser_set_max_quoted(json_parser, 10);
#endif

	/* just load into memory, no extraction performed */
	priv->json_node = fwupd_json_parser_load_from_stream(json_parser,
							     stream,
							     FWUPD_JSON_LOAD_FLAG_NONE,
							     error);
	if (priv->json_node == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static GByteArray *
fu_json_firmware_write(FuFirmware *firmware, GError **error)
{
	FuJsonFirmware *self = FU_JSON_FIRMWARE(firmware);
	FuJsonFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GString) str = NULL;

	/* sanity check */
	if (priv->json_node == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "no JSON node");
		return NULL;
	}

	/* export with no padding */
	str = fwupd_json_node_to_string(priv->json_node, FWUPD_JSON_EXPORT_FLAG_NONE);
	g_byte_array_append(buf, (const guint8 *)str->str, str->len);
	return g_steal_pointer(&buf);
}

static void
fu_json_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuJsonFirmware *self = FU_JSON_FIRMWARE(firmware);
	FuJsonFirmwarePrivate *priv = GET_PRIVATE(self);
	if (priv->json_node != NULL) {
		g_autoptr(GString) str =
		    fwupd_json_node_to_string(priv->json_node, FWUPD_JSON_EXPORT_FLAG_NONE);
		fu_xmlb_builder_insert_kv(bn, "json", str->str);
	}
}

static gboolean
fu_json_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuJsonFirmware *self = FU_JSON_FIRMWARE(firmware);
	FuJsonFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *json;
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();

	/* simple properties */
	json = xb_node_query_text(n, "json", error);
	if (json == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}
	priv->json_node =
	    fwupd_json_parser_load_from_data(json_parser, json, FWUPD_JSON_LOAD_FLAG_NONE, error);
	if (priv->json_node == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_json_firmware_init(FuJsonFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_json_firmware_finalize(GObject *obj)
{
	FuJsonFirmware *self = FU_JSON_FIRMWARE(obj);
	FuJsonFirmwarePrivate *priv = GET_PRIVATE(self);
	if (priv->json_node != NULL)
		fwupd_json_node_unref(priv->json_node);
	G_OBJECT_CLASS(fu_json_firmware_parent_class)->finalize(obj);
}

static void
fu_json_firmware_class_init(FuJsonFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_json_firmware_finalize;
	firmware_class->parse = fu_json_firmware_parse;
	firmware_class->write = fu_json_firmware_write;
	firmware_class->build = fu_json_firmware_build;
	firmware_class->export = fu_json_firmware_export;
}
