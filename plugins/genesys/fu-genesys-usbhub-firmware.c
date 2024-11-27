/*
 * Copyright 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 * Copyright 2021 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-genesys-common.h"
#include "fu-genesys-usbhub-codesign-firmware.h"
#include "fu-genesys-usbhub-dev-firmware.h"
#include "fu-genesys-usbhub-firmware.h"
#include "fu-genesys-usbhub-pd-firmware.h"
#include "fu-genesys-usbhub-struct.h"

struct _FuGenesysUsbhubFirmware {
	FuFirmwareClass parent_instance;
	GByteArray *st_static_ts;
	FuGenesysChip chip;
};

G_DEFINE_TYPE(FuGenesysUsbhubFirmware, fu_genesys_usbhub_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_genesys_usbhub_firmware_get_chip(FuGenesysUsbhubFirmware *self,
				    GInputStream *stream,
				    GError **error)
{
	guint8 project_ic_type[6];

	/* recognize GL3523 code base product */
	if (!fu_input_stream_read_safe(
		stream,
		project_ic_type,
		sizeof(project_ic_type),
		0, /* dst */
		GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3523 +
		    FU_STRUCT_GENESYS_TS_STATIC_OFFSET_MASK_PROJECT_IC_TYPE, /* src */
		sizeof(project_ic_type),
		error))
		return FALSE;

	if (memcmp(project_ic_type, "3521", 4) == 0) {
		self->chip.model = ISP_MODEL_HUB_GL3521;
		self->chip.revision = 10 * (project_ic_type[4] - '0') + (project_ic_type[5] - '0');
		return TRUE;
	}

	if (memcmp(project_ic_type, "3523", 4) == 0) {
		self->chip.model = ISP_MODEL_HUB_GL3523;
		self->chip.revision = 10 * (project_ic_type[4] - '0') + (project_ic_type[5] - '0');
		return TRUE;
	}

	/* recognize GL3590 */
	if (!fu_input_stream_read_safe(
		stream,
		project_ic_type,
		sizeof(project_ic_type),
		0, /* dst */
		GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3590 +
		    FU_STRUCT_GENESYS_TS_STATIC_OFFSET_MASK_PROJECT_IC_TYPE, /* src */
		sizeof(project_ic_type),
		error))
		return FALSE;

	if (memcmp(project_ic_type, "3590", 4) == 0) {
		self->chip.model = ISP_MODEL_HUB_GL3590;
		self->chip.revision = 10 * (project_ic_type[4] - '0') + (project_ic_type[5] - '0');
		return TRUE;
	}

	/* recognize GL3525 first edition */
	if (!fu_input_stream_read_safe(
		stream,
		project_ic_type,
		sizeof(project_ic_type),
		0, /* dst */
		GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3525 +
		    FU_STRUCT_GENESYS_TS_STATIC_OFFSET_MASK_PROJECT_IC_TYPE, /* src */
		sizeof(project_ic_type),
		error))
		return FALSE;

	if (memcmp(project_ic_type, "3525", 4) == 0) {
		self->chip.model = ISP_MODEL_HUB_GL3525;
		self->chip.revision = 10 * (project_ic_type[4] - '0') + (project_ic_type[5] - '0');
		return TRUE;
	}

	/* recognize GL3525 second edition */
	if (!fu_input_stream_read_safe(
		stream,
		project_ic_type,
		sizeof(project_ic_type),
		0, /* dst */
		GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3525_V2 +
		    FU_STRUCT_GENESYS_TS_STATIC_OFFSET_MASK_PROJECT_IC_TYPE, /* src */
		sizeof(project_ic_type),
		error))
		return FALSE;

	if (memcmp(project_ic_type, "3525", 4) == 0) {
		self->chip.model = ISP_MODEL_HUB_GL3525;
		self->chip.revision = 10 * (project_ic_type[4] - '0') + (project_ic_type[5] - '0');
		return TRUE;
	}

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "unsupported IC");
	return FALSE;
}

gboolean
fu_genesys_usbhub_firmware_verify_checksum(GInputStream *stream, GError **error)
{
	gsize streamsz = 0;
	guint16 fw_checksum = 0;
	guint16 checksum = 0;
	g_autoptr(GInputStream) stream_tmp = NULL;

	/* get checksum */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < sizeof(checksum)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "stream was too small");
		return FALSE;
	}
	if (!fu_input_stream_read_u16(stream,
				      streamsz - sizeof(checksum),
				      &fw_checksum,
				      G_BIG_ENDIAN,
				      error)) {
		g_prefix_error(error, "failed to get checksum: ");
		return FALSE;
	}

	/* calculate checksum */
	stream_tmp = fu_partial_input_stream_new(stream, 0, streamsz - sizeof(checksum), error);
	if (stream_tmp == NULL)
		return FALSE;
	if (!fu_input_stream_compute_sum16(stream_tmp, &checksum, error))
		return FALSE;
	if (checksum != fw_checksum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "checksum mismatch, got 0x%04x, expected 0x%04x",
			    checksum,
			    fw_checksum);
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_genesys_usbhub_firmware_calculate_size(GInputStream *stream, gsize *size, GError **error)
{
	guint8 kbs = 0;
	if (!fu_input_stream_read_u8(stream, GENESYS_USBHUB_CODE_SIZE_OFFSET, &kbs, error)) {
		g_prefix_error(error, "failed to get codesize: ");
		return FALSE;
	}
	if (kbs == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid codesize");
		return FALSE;
	}
	if (size != NULL)
		*size = 1024 * kbs;
	return TRUE;
}

gboolean
fu_genesys_usbhub_firmware_ensure_version(FuFirmware *firmware, GError **error)
{
	guint16 version_raw = 0;
	g_autoptr(GBytes) fw = NULL;

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	if (!fu_memread_uint16_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    GENESYS_USBHUB_VERSION_OFFSET,
				    &version_raw,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to get version: ");
		return FALSE;
	}
	fu_firmware_set_version_raw(firmware, version_raw);

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_firmware_validate(FuFirmware *firmware,
				    GInputStream *stream,
				    gsize offset,
				    GError **error)
{
	return fu_struct_genesys_firmware_hdr_validate_stream(stream, offset, error);
}

static gboolean
fu_genesys_usbhub_firmware_parse(FuFirmware *firmware,
				 GInputStream *stream,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuGenesysUsbhubFirmware *self = FU_GENESYS_USBHUB_FIRMWARE(firmware);
	gsize code_size = 0;
	gsize offset = 0;
	gsize streamsz = 0;
	guint32 static_ts_offset = 0;
	g_autoptr(GInputStream) stream_trunc = NULL;

	/* get chip */
	if (!fu_genesys_usbhub_firmware_get_chip(self, stream, error)) {
		g_prefix_error(error, "failed to get chip: ");
		return FALSE;
	}
	fu_firmware_set_id(firmware, fu_genesys_fw_type_to_string(FU_GENESYS_FW_TYPE_HUB));
	fu_firmware_set_idx(firmware, FU_GENESYS_FW_TYPE_HUB);
	fu_firmware_set_alignment(firmware, FU_FIRMWARE_ALIGNMENT_1K);

	/* get static tool string */
	switch (self->chip.model) {
	case ISP_MODEL_HUB_GL3521:
		static_ts_offset = GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3521;
		break;
	case ISP_MODEL_HUB_GL3523:
		static_ts_offset = GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3523;
		break;
	case ISP_MODEL_HUB_GL3590:
		static_ts_offset = GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3590;
		break;
	case ISP_MODEL_HUB_GL3525: {
		guint8 configuration = 0;
		if (!fu_input_stream_read_u8(stream,
					     GENESYS_USBHUB_FW_CONFIGURATION_OFFSET,
					     &configuration,
					     error))
			return FALSE;
		if (configuration == GENESYS_USBHUB_FW_CONFIGURATION_NEW_FORMAT ||
		    configuration == GENESYS_USBHUB_FW_CONFIGURATION_NEW_FORMAT_V2)
			static_ts_offset = GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3525_V2;
		else
			static_ts_offset = GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3525;
		break;
	}
	default:
		break;
	}
	self->st_static_ts =
	    fu_struct_genesys_ts_static_parse_stream(stream, static_ts_offset, error);

	/* deduce code size */
	switch (self->chip.model) {
	case ISP_MODEL_HUB_GL3521:
		code_size = 0x5000;
		break;
	case ISP_MODEL_HUB_GL3523: {
		if (self->chip.revision == 50) {
			if (!fu_genesys_usbhub_firmware_calculate_size(stream, &code_size, error))
				return FALSE;
		} else {
			code_size = 0x6000;
		}
		break;
	}
	case ISP_MODEL_HUB_GL3590:
	case ISP_MODEL_HUB_GL3525: {
		if (!fu_genesys_usbhub_firmware_calculate_size(stream, &code_size, error))
			return FALSE;
		break;
	}
	default:
		break;
	}

	/* truncate to correct size */
	stream_trunc = fu_partial_input_stream_new(stream, offset, code_size, error);
	if (stream_trunc == NULL)
		return FALSE;
	if (!fu_firmware_set_stream(firmware, stream_trunc, error))
		return FALSE;

	/* calculate checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		if (!fu_genesys_usbhub_firmware_verify_checksum(stream_trunc, error))
			return FALSE;
	}

	/* get firmware version */
	if (!fu_genesys_usbhub_firmware_ensure_version(firmware, error))
		return FALSE;

	/* parse remaining firmware bytes */
	offset += code_size;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	while (offset < streamsz) {
		g_autoptr(FuFirmware) firmware_sub = NULL;
		firmware_sub = fu_firmware_new_from_gtypes(stream,
							   offset,
							   flags | FWUPD_INSTALL_FLAG_NO_SEARCH,
							   error,
							   FU_TYPE_GENESYS_USBHUB_DEV_FIRMWARE,
							   FU_TYPE_GENESYS_USBHUB_PD_FIRMWARE,
							   FU_TYPE_GENESYS_USBHUB_CODESIGN_FIRMWARE,
							   G_TYPE_INVALID);
		if (firmware_sub == NULL) {
			g_prefix_error(error, "fw bytes have dual hub firmware: ");
			return FALSE;
		}
		fu_firmware_set_offset(firmware_sub, offset);
		fu_firmware_add_image(firmware, firmware_sub);
		offset += fu_firmware_get_size(firmware_sub);
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_genesys_usbhub_firmware_write(FuFirmware *firmware, GError **error)
{
	FuGenesysUsbhubFirmware *self = FU_GENESYS_USBHUB_FIRMWARE(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	guint16 code_size = 0x6000;
	guint16 checksum;

	/* fixed size */
	fu_byte_array_set_size(buf, code_size, 0x00);

	/* signature */
	if (!fu_memcpy_safe(buf->data,
			    buf->len,
			    FU_STRUCT_GENESYS_FIRMWARE_HDR_OFFSET_MAGIC, /* dst */
			    (const guint8 *)FU_STRUCT_GENESYS_FIRMWARE_HDR_DEFAULT_MAGIC,
			    FU_STRUCT_GENESYS_FIRMWARE_HDR_SIZE_MAGIC,
			    0x0, /* src */
			    FU_STRUCT_GENESYS_FIRMWARE_HDR_SIZE_MAGIC,
			    error))
		return NULL;

	/* static tool string */
	if (self->st_static_ts != NULL) {
		if (!fu_memcpy_safe(buf->data,
				    buf->len,
				    GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3523, /* dst */
				    self->st_static_ts->data,
				    self->st_static_ts->len,
				    0x0, /* src */
				    self->st_static_ts->len,
				    error))
			return NULL;
	}

	/* version */
	if (!fu_memwrite_uint16_safe(buf->data,
				     buf->len,
				     GENESYS_USBHUB_VERSION_OFFSET,
				     0x1234, // TODO: parse from firmware version string
				     G_BIG_ENDIAN,
				     error))
		return NULL;

	/* checksum */
	checksum = fu_sum16(buf->data, code_size - sizeof(checksum));
	if (!fu_memwrite_uint16_safe(buf->data,
				     buf->len,
				     code_size - sizeof(guint16),
				     checksum,
				     G_BIG_ENDIAN,
				     error))
		return NULL;

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_genesys_usbhub_firmware_export(FuFirmware *firmware,
				  FuFirmwareExportFlags flags,
				  XbBuilderNode *bn)
{
	FuGenesysUsbhubFirmware *self = FU_GENESYS_USBHUB_FIRMWARE(firmware);
	if (self->st_static_ts != NULL) {
		FuGenesysTsVersion tool_string_version =
		    fu_struct_genesys_ts_static_get_tool_string_version(self->st_static_ts);
		g_autofree gchar *mask_project_code =
		    fu_struct_genesys_ts_static_get_mask_project_code(self->st_static_ts);
		g_autofree gchar *mask_project_hardware =
		    fu_struct_genesys_ts_static_get_mask_project_hardware(self->st_static_ts);
		g_autofree gchar *mask_project_firmware =
		    fu_struct_genesys_ts_static_get_mask_project_firmware(self->st_static_ts);
		g_autofree gchar *mask_project_ic_type =
		    fu_struct_genesys_ts_static_get_mask_project_ic_type(self->st_static_ts);
		g_autofree gchar *running_project_code =
		    fu_struct_genesys_ts_static_get_mask_project_code(self->st_static_ts);
		g_autofree gchar *running_project_hardware =
		    fu_struct_genesys_ts_static_get_running_project_hardware(self->st_static_ts);
		g_autofree gchar *running_project_firmware =
		    fu_struct_genesys_ts_static_get_running_project_firmware(self->st_static_ts);
		g_autofree gchar *running_project_ic_type =
		    fu_struct_genesys_ts_static_get_running_project_ic_type(self->st_static_ts);

		fu_xmlb_builder_insert_kv(bn,
					  "tool_string_version",
					  fu_genesys_ts_version_to_string(tool_string_version));
		fu_xmlb_builder_insert_kv(bn, "mask_project_code", mask_project_code);
		if (mask_project_hardware != NULL)
			mask_project_hardware[0] += 0x11; /* '0' -> 'A'... */
		fu_xmlb_builder_insert_kv(bn, "mask_project_hardware", mask_project_hardware);
		fu_xmlb_builder_insert_kv(bn, "mask_project_firmware", mask_project_firmware);
		fu_xmlb_builder_insert_kv(bn, "mask_project_ic_type", mask_project_ic_type);
		fu_xmlb_builder_insert_kv(bn, "running_project_code", running_project_code);
		if (running_project_hardware != NULL)
			running_project_hardware[0] += 0x11; /* '0' -> 'A'... */
		fu_xmlb_builder_insert_kv(bn, "running_project_hardware", running_project_hardware);
		fu_xmlb_builder_insert_kv(bn, "running_project_firmware", running_project_firmware);
		fu_xmlb_builder_insert_kv(bn, "running_project_ic_type", running_project_ic_type);
	}
}

static gboolean
fu_genesys_usbhub_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuGenesysUsbhubFirmware *self = FU_GENESYS_USBHUB_FIRMWARE(firmware);
	const gchar *tmp;

	/* optional properties */
	self->st_static_ts = fu_struct_genesys_ts_static_new();

	tmp = xb_node_query_text(n, "tool_string_version", NULL);
	if (tmp == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid tool_string_version");
		return FALSE;
	} else {
		fu_struct_genesys_ts_static_set_tool_string_version(self->st_static_ts, tmp[0]);
	}

	/* mask_project_code */
	tmp = xb_node_query_text(n, "mask_project_code", NULL);
	if (tmp != NULL) {
		gsize len = strlen(tmp);
		if (len != 4) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid mask_project_code %s, got 0x%x length",
				    tmp,
				    (guint)len);
			return FALSE;
		}
		if (!fu_struct_genesys_ts_static_set_mask_project_code(self->st_static_ts,
								       tmp,
								       error))
			return FALSE;
	}

	/* mask_project_ic_type */
	tmp = xb_node_query_text(n, "mask_project_ic_type", NULL);
	if (tmp != NULL) {
		gsize len = strlen(tmp);
		if (len != 6) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid mask_project_ic_type %s, got 0x%x length",
				    tmp,
				    (guint)len);
			return FALSE;
		}
		if (!fu_struct_genesys_ts_static_set_mask_project_ic_type(self->st_static_ts,
									  tmp,
									  error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gchar *
fu_genesys_usbhub_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint16_hex(version_raw, fu_firmware_get_version_format(firmware));
}

static void
fu_genesys_usbhub_firmware_init(FuGenesysUsbhubFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_genesys_usbhub_firmware_finalize(GObject *object)
{
	FuGenesysUsbhubFirmware *self = FU_GENESYS_USBHUB_FIRMWARE(object);
	if (self->st_static_ts != NULL)
		fu_struct_genesys_ts_static_unref(self->st_static_ts);
	G_OBJECT_CLASS(fu_genesys_usbhub_firmware_parent_class)->finalize(object);
}

static void
fu_genesys_usbhub_firmware_class_init(FuGenesysUsbhubFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->convert_version = fu_genesys_usbhub_firmware_convert_version;
	object_class->finalize = fu_genesys_usbhub_firmware_finalize;
	firmware_class->validate = fu_genesys_usbhub_firmware_validate;
	firmware_class->parse = fu_genesys_usbhub_firmware_parse;
	firmware_class->export = fu_genesys_usbhub_firmware_export;
	firmware_class->build = fu_genesys_usbhub_firmware_build;
	firmware_class->write = fu_genesys_usbhub_firmware_write;
}

FuFirmware *
fu_genesys_usbhub_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL));
}
