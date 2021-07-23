/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <json-glib/json-glib.h>

#include <fwupdplugin.h>

#include "fu-solokey-firmware.h"

struct _FuSolokeyFirmware {
	FuFirmware		 parent_instance;
};

G_DEFINE_TYPE (FuSolokeyFirmware, fu_solokey_firmware, FU_TYPE_FIRMWARE)

static GBytes *
_g_base64_decode_to_bytes (const gchar *text)
{
	gsize out_len = 0;
	guchar *out = g_base64_decode (text, &out_len);
	return g_bytes_new_take ((guint8 *) out, out_len);
}

static gboolean
fu_solokey_firmware_parse (FuFirmware *firmware,
			   GBytes *fw,
			   guint64 addr_start,
			   guint64 addr_end,
			   FwupdInstallFlags flags,
			   GError **error)
{
	JsonNode *json_root;
	JsonObject *json_obj;
	const gchar *base64;
	g_autoptr(FuFirmware) ihex_firmware = fu_ihex_firmware_new ();
	g_autoptr(FuFirmware) img_sig = fu_firmware_new ();
	g_autoptr(GBytes) fw_ihex = NULL;
	g_autoptr(GBytes) fw_sig = NULL;
	g_autoptr(GString) base64_websafe = NULL;
	g_autoptr(JsonParser) parser = json_parser_new ();

	/* parse JSON */
	if (!json_parser_load_from_data (parser,
					 (const gchar *) g_bytes_get_data (fw, NULL),
					 (gssize) g_bytes_get_size (fw),
					 error)) {
		g_prefix_error (error, "firmware not in JSON format: ");
		return FALSE;
	}
	json_root = json_parser_get_root (parser);
	if (json_root == NULL || !JSON_NODE_HOLDS_OBJECT (json_root)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "JSON invalid as has no root");
		return FALSE;
	}
	json_obj = json_node_get_object (json_root);
	if (!json_object_has_member (json_obj, "firmware")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "JSON invalid as has no 'firmware'");
		return FALSE;
	}
	if (!json_object_has_member (json_obj, "signature")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "JSON invalid as has no 'signature'");
		return FALSE;
	}

	/* decode */
	base64 = json_object_get_string_member (json_obj, "firmware");
	if (base64 == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "JSON 'firmware' missing");
		return FALSE;
	}
	fw_ihex = _g_base64_decode_to_bytes (base64);
	if (!fu_firmware_parse (ihex_firmware, fw_ihex, flags, error))
		return FALSE;
	fw = fu_firmware_get_bytes (ihex_firmware, error);
	if (fw == NULL)
		return FALSE;
	fu_firmware_set_addr (firmware, fu_firmware_get_addr (ihex_firmware));
	fu_firmware_set_bytes (firmware, fw);

	/* signature */
	base64 = json_object_get_string_member (json_obj, "signature");
	if (base64 == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "JSON 'signature' missing");
		return FALSE;
	}
	base64_websafe = g_string_new (base64);
	fu_common_string_replace (base64_websafe, "-", "+");
	fu_common_string_replace (base64_websafe, "_", "/");
	g_string_append (base64_websafe, "==");
	fw_sig = _g_base64_decode_to_bytes (base64_websafe->str);
	fu_firmware_set_bytes (img_sig, fw_sig);
	fu_firmware_set_id (img_sig, FU_FIRMWARE_ID_SIGNATURE);
	fu_firmware_add_image (firmware, img_sig);
	return TRUE;
}

static GBytes *
fu_solokey_firmware_write (FuFirmware *firmware, GError **error)
{
	g_autofree gchar *buf_base64 = NULL;
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) buf_blob = NULL;
	g_autoptr(GString) str = g_string_new (NULL);
	g_autoptr(JsonBuilder) builder = json_builder_new ();
	g_autoptr(JsonGenerator) json_generator = json_generator_new ();
	g_autoptr(JsonNode) json_root = NULL;

	/* default image */
	json_builder_begin_object (builder);
	buf_blob = fu_firmware_get_bytes (firmware, error);
	if (buf_blob == NULL)
		return NULL;
	buf_base64 = g_base64_encode ((const guchar *) g_bytes_get_data (buf_blob, NULL),
				      g_bytes_get_size (buf_blob));
	json_builder_set_member_name (builder, "firmware");
	json_builder_add_string_value (builder, buf_base64);

	/* optional signature */
	img = fu_firmware_get_image_by_id (firmware, FU_FIRMWARE_ID_SIGNATURE, error);
	if (img != NULL) {
		g_autoptr(GBytes) sig_blob = NULL;
		g_autofree gchar *sig_base64 = NULL;
		sig_blob = fu_firmware_get_bytes (img, error);
		if (sig_blob == NULL)
			return NULL;
		sig_base64 = g_base64_encode ((const guchar *) g_bytes_get_data (sig_blob, NULL),
					      g_bytes_get_size (sig_blob));
		json_builder_set_member_name (builder, "signature");
		json_builder_add_string_value (builder, sig_base64);
	}
	json_builder_end_object (builder);

	/* output as a string */
	json_root = json_builder_get_root (builder);
	json_generator_set_root (json_generator, json_root);
	json_generator_to_gstring (json_generator, str);
	g_string_append_c (str, '\n');

	/* success */
	return g_string_free_to_bytes (g_steal_pointer (&str));
}

static void
fu_solokey_firmware_init (FuSolokeyFirmware *self)
{
}

static void
fu_solokey_firmware_class_init (FuSolokeyFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_solokey_firmware_parse;
	klass_firmware->write = fu_solokey_firmware_write;
}

FuFirmware *
fu_solokey_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_SOLOKEY_FIRMWARE, NULL));
}
