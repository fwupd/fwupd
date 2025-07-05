/*
 * Copyright 2021 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-genesys-common.h"
#include "fu-genesys-scaler-firmware.h"

struct _FuGenesysScalerFirmware {
	FuFirmwareClass parent_instance;
	FuGenesysPublicKey public_key;
};

G_DEFINE_TYPE(FuGenesysScalerFirmware, fu_genesys_scaler_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_genesys_scaler_firmware_parse(FuFirmware *firmware,
				 GInputStream *stream,
				 FuFirmwareParseFlags flags,
				 GError **error)
{
	FuGenesysScalerFirmware *self = FU_GENESYS_SCALER_FIRMWARE(firmware);
	gsize streamsz = 0;
	g_autoptr(FuFirmware) firmware_payload = fu_firmware_new();
	g_autoptr(FuFirmware) firmware_public_key = NULL;
	g_autoptr(GInputStream) stream_payload = NULL;
	g_autoptr(GBytes) blob_public_key = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < sizeof(self->public_key)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "image is too small");
		return FALSE;
	}
	if (!fu_input_stream_read_safe(stream,
				       (guint8 *)&self->public_key,
				       sizeof(self->public_key),
				       0,				    /* dst */
				       streamsz - sizeof(self->public_key), /* src */
				       sizeof(self->public_key),
				       error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN,
		    "PublicKey",
		    (const guint8 *)&self->public_key,
		    sizeof(self->public_key));
	if (memcmp(self->public_key.N, "N = ", 4) != 0 ||
	    memcmp(self->public_key.E, "E = ", 4) != 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "invalid public-key");
		return FALSE;
	}

	/* set payload */
	stream_payload =
	    fu_partial_input_stream_new(stream, 0, streamsz - sizeof(self->public_key), error);
	if (stream_payload == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(firmware_payload, stream_payload, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_id(firmware_payload, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_add_image(firmware, firmware_payload);

	/* set public-key */
	blob_public_key = g_bytes_new(&self->public_key, sizeof(self->public_key));
	firmware_public_key = fu_firmware_new_from_bytes(blob_public_key);
	fu_firmware_set_id(firmware_public_key, FU_FIRMWARE_ID_SIGNATURE);
	fu_firmware_add_image(firmware, firmware_public_key);

	/* success */
	return TRUE;
}

static void
fu_genesys_scaler_firmware_export(FuFirmware *firmware,
				  FuFirmwareExportFlags flags,
				  XbBuilderNode *bn)
{
	FuGenesysScalerFirmware *self = FU_GENESYS_SCALER_FIRMWARE(firmware);
	gchar N[0x200 + 1] = {'\0'};
	gchar E[0x006 + 1] = {'\0'};

	memcpy(N, self->public_key.N + 4, sizeof(N) - 1); /* nocheck:blocked */
	fu_xmlb_builder_insert_kv(bn, "N", N);

	memcpy(E, self->public_key.E + 4, sizeof(E) - 1); /* nocheck:blocked */
	fu_xmlb_builder_insert_kv(bn, "E", E);
}

static gboolean
fu_genesys_scaler_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuGenesysScalerFirmware *self = FU_GENESYS_SCALER_FIRMWARE(firmware);
	const gchar *tmp;

	/* optional properties */
	tmp = xb_node_query_text(n, "N", NULL);
	if (tmp != NULL) {
		if (!fu_memcpy_safe((guint8 *)&self->public_key.N,
				    sizeof(self->public_key.N),
				    0x0, /* dst */
				    (const guint8 *)tmp,
				    strlen(tmp),
				    0x0, /* src */
				    strlen(tmp),
				    error))
			return FALSE;
	}

	tmp = xb_node_query_text(n, "E", NULL);
	if (tmp != NULL) {
		if (!fu_memcpy_safe((guint8 *)&self->public_key.E,
				    sizeof(self->public_key.E),
				    0x0, /* dst */
				    (const guint8 *)tmp,
				    strlen(tmp),
				    0x0, /* src */
				    strlen(tmp),
				    error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_genesys_scaler_firmware_write(FuFirmware *firmware, GError **error)
{
	FuGenesysScalerFirmware *self = FU_GENESYS_SCALER_FIRMWARE(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;

	/* payload */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob);

	/* public-key */
	g_byte_array_append(buf, (const guint8 *)&self->public_key, sizeof(self->public_key));

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_genesys_scaler_firmware_init(FuGenesysScalerFirmware *self)
{
}

static void
fu_genesys_scaler_firmware_class_init(FuGenesysScalerFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_genesys_scaler_firmware_parse;
	firmware_class->export = fu_genesys_scaler_firmware_export;
	firmware_class->build = fu_genesys_scaler_firmware_build;
	firmware_class->write = fu_genesys_scaler_firmware_write;
}

FuFirmware *
fu_genesys_scaler_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_GENESYS_SCALER_FIRMWARE, NULL));
}
