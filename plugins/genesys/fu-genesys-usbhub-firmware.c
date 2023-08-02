/*
 * Copyright (C) 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 * Copyright (C) 2021 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
				    const guint8 *buf,
				    gsize bufsz,
				    gsize offset,
				    GError **error)
{
	guint8 project_ic_type[6];

	/* recognize GL3523 code base product */
	if (!fu_memcpy_safe(project_ic_type,
			    sizeof(project_ic_type),
			    0, /* dst */
			    buf,
			    bufsz,
			    offset + GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3523 +
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
	if (!fu_memcpy_safe(project_ic_type,
			    sizeof(project_ic_type),
			    0, /* dst */
			    buf,
			    bufsz,
			    offset + GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3590 +
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
	if (!fu_memcpy_safe(project_ic_type,
			    sizeof(project_ic_type),
			    0, /* dst */
			    buf,
			    bufsz,
			    offset + GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3525 +
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
	if (!fu_memcpy_safe(project_ic_type,
			    sizeof(project_ic_type),
			    0, /* dst */
			    buf,
			    bufsz,
			    offset + GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3525_V2 +
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
fu_genesys_usbhub_firmware_verify_checksum(GBytes *fw, GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	guint16 fw_checksum = 0;
	guint16 checksum;

	/* get checksum */
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    bufsz - sizeof(checksum),
				    &fw_checksum,
				    G_BIG_ENDIAN,
				    error)) {
		g_prefix_error_literal(error, "failed to get checksum: ");
		return FALSE;
	}

	/* calculate checksum */
	checksum = fu_sum16(buf, bufsz - sizeof(checksum));
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
fu_genesys_usbhub_firmware_calculate_size(GBytes *fw, gsize offset, gsize *size, GError **error)
{
	guint8 kbs = 0;
	if (!fu_memread_uint8_safe(g_bytes_get_data(fw, NULL),
				   g_bytes_get_size(fw),
				   offset + GENESYS_USBHUB_CODE_SIZE_OFFSET,
				   &kbs,
				   error)) {
		g_prefix_error_literal(error, "failed to get codesize: ");
		return FALSE;
	}
	if (kbs == 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "invalid codesize");
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
	g_autofree gchar *version = NULL;
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
		g_prefix_error_literal(error, "failed to get version: ");
		return FALSE;
	}
	fu_firmware_set_version_raw(firmware, version_raw);
	version =
	    g_strdup_printf("%02x.%02x", (version_raw & 0xFF00U) >> 8, (version_raw & 0x00FFU));
	fu_firmware_set_version(firmware, version);

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_firmware_check_magic(FuFirmware *firmware,
				       GBytes *fw,
				       gsize offset,
				       GError **error)
{
	guint8 magic[4] = GENESYS_USBHUB_FW_SIG_TEXT_HUB;
	return fu_memcmp_safe(g_bytes_get_data(fw, NULL),
			      g_bytes_get_size(fw),
			      offset + GENESYS_USBHUB_FW_SIG_OFFSET,
			      magic,
			      sizeof(magic),
			      0x0,
			      sizeof(magic),
			      error);
}

static gboolean
fu_genesys_usbhub_firmware_parse(FuFirmware *firmware,
				 GBytes *fw,
				 gsize offset,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuGenesysUsbhubFirmware *self = FU_GENESYS_USBHUB_FIRMWARE(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	gsize code_size = 0;
	guint32 static_ts_offset = 0;
	g_autoptr(GBytes) fw_trunc = NULL;

	/* get chip */
	if (!fu_genesys_usbhub_firmware_get_chip(self, buf, bufsz, offset, error)) {
		g_prefix_error_literal(error, "failed to get chip: ");
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
		if (!fu_memread_uint8_safe(buf,
					   bufsz,
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
	self->st_static_ts = fu_struct_genesys_ts_static_parse(buf, bufsz, static_ts_offset, error);

	/* deduce code size */
	switch (self->chip.model) {
	case ISP_MODEL_HUB_GL3521:
		code_size = 0x5000;
		break;
	case ISP_MODEL_HUB_GL3523: {
		if (self->chip.revision == 50) {
			if (!fu_genesys_usbhub_firmware_calculate_size(fw,
								       offset,
								       &code_size,
								       error))
				return FALSE;
		} else {
			code_size = 0x6000;
		}
		break;
	}
	case ISP_MODEL_HUB_GL3590:
	case ISP_MODEL_HUB_GL3525: {
		if (!fu_genesys_usbhub_firmware_calculate_size(fw, offset, &code_size, error))
			return FALSE;
		break;
	}
	default:
		break;
	}

	/* truncate to correct size */
	fw_trunc = fu_bytes_new_offset(fw, offset, code_size, error);
	if (fw_trunc == NULL)
		return FALSE;
	fu_firmware_set_bytes(firmware, fw_trunc);

	/* calculate checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		if (!fu_genesys_usbhub_firmware_verify_checksum(fw_trunc, error))
			return FALSE;
	}

	/* get firmware version */
	if (!fu_genesys_usbhub_firmware_ensure_version(firmware, error))
		return FALSE;

	/* parse remaining firmware bytes */
	offset += code_size;
	while (offset < bufsz) {
		g_autoptr(FuFirmware) firmware_sub = NULL;
		firmware_sub = fu_firmware_new_from_gtypes(fw,
							   offset,
							   flags | FWUPD_INSTALL_FLAG_NO_SEARCH,
							   error,
							   FU_TYPE_GENESYS_USBHUB_DEV_FIRMWARE,
							   FU_TYPE_GENESYS_USBHUB_PD_FIRMWARE,
							   FU_TYPE_GENESYS_USBHUB_CODESIGN_FIRMWARE,
							   G_TYPE_INVALID);
		if (firmware_sub == NULL) {
			g_prefix_error_literal(error, "fw bytes have dual hub firmware: ");
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
			    GENESYS_USBHUB_FW_SIG_OFFSET, /* dst */
			    (const guint8 *)GENESYS_USBHUB_FW_SIG_TEXT_HUB,
			    GENESYS_USBHUB_FW_SIG_LEN,
			    0x0, /* src */
			    GENESYS_USBHUB_FW_SIG_LEN,
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

	/* checksum */
	checksum = fu_sum16(buf->data, code_size - sizeof(checksum));
	if (!fu_memwrite_uint16_safe(buf->data,
				     buf->len,
				     code_size - sizeof(guint16),
				     checksum,
				     G_BIG_ENDIAN,
				     error))
		return NULL;

	/* version */
	if (!fu_memwrite_uint16_safe(buf->data,
				     buf->len,
				     GENESYS_USBHUB_VERSION_OFFSET,
				     0x1234, // TODO: parse from firmware version string
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
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
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
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
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
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
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

static void
fu_genesys_usbhub_firmware_init(FuGenesysUsbhubFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_genesys_usbhub_firmware_finalize(GObject *object)
{
	FuGenesysUsbhubFirmware *self = FU_GENESYS_USBHUB_FIRMWARE(object);
	if (self->st_static_ts != NULL)
		g_byte_array_unref(self->st_static_ts);
	G_OBJECT_CLASS(fu_genesys_usbhub_firmware_parent_class)->finalize(object);
}

static void
fu_genesys_usbhub_firmware_class_init(FuGenesysUsbhubFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_genesys_usbhub_firmware_finalize;
	klass_firmware->check_magic = fu_genesys_usbhub_firmware_check_magic;
	klass_firmware->parse = fu_genesys_usbhub_firmware_parse;
	klass_firmware->export = fu_genesys_usbhub_firmware_export;
	klass_firmware->build = fu_genesys_usbhub_firmware_build;
	klass_firmware->write = fu_genesys_usbhub_firmware_write;
}

FuFirmware *
fu_genesys_usbhub_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL));
}
