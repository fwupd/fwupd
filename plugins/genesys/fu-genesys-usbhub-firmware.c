/*
 * Copyright (C) 2021 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-genesys-common.h"
#include "fu-genesys-usbhub-firmware.h"

struct _FuGenesysUsbhubFirmware {
	FuFirmwareClass parent_instance;
	FuGenesysStaticToolString static_ts;
	FuGenesysChip chip;
};

G_DEFINE_TYPE(FuGenesysUsbhubFirmware, fu_genesys_usbhub_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_genesys_usbhub_firmware_get_chip(FuGenesysUsbhubFirmware *self,
				    const guint8 *buf,
				    gsize bufsz,
				    GError **error)
{
	guint8 project_ic_type[6];

	if (!fu_memcpy_safe(project_ic_type,
			    sizeof(project_ic_type),
			    0, /* dst */
			    buf,
			    bufsz,
			    GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3523 + 8, /* src */
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

	if (!fu_memcpy_safe(project_ic_type,
			    sizeof(project_ic_type),
			    0, /* dst */
			    buf,
			    bufsz,
			    GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3590 + 8, /* src */
			    sizeof(project_ic_type),
			    error))
		return FALSE;

	if (memcmp(project_ic_type, "3590", 4) == 0) {
		self->chip.model = ISP_MODEL_HUB_GL3590;
		self->chip.revision = 10 * (project_ic_type[4] - '0') + (project_ic_type[5] - '0');
		return TRUE;
	}

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "Unsupported IC");
	return FALSE;
}

static gboolean
fu_genesys_usbhub_firmware_verify(const guint8 *buf, gsize bufsz, guint16 code_size, GError **error)
{
	guint16 fw_checksum, checksum;

	/* check code-size */
	if (code_size < sizeof(checksum)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "code-size is too small: %u bytes",
			    code_size);
		return FALSE;
	}

	/* get checksum */
	if (!fu_common_read_uint16_safe(buf,
					bufsz,
					code_size - sizeof(checksum),
					&fw_checksum,
					G_BIG_ENDIAN,
					error))
		return FALSE;

	/* calculate checksum */
	checksum = fu_common_sum16(buf, code_size - sizeof(checksum));
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

static gboolean
fu_genesys_usbhub_firmware_parse(FuFirmware *firmware,
				 GBytes *fw,
				 guint64 addr_start,
				 guint64 addr_end,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuGenesysUsbhubFirmware *self = FU_GENESYS_USBHUB_FIRMWARE(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	guint8 sign[4];
	guint32 code_size = 0;
	guint32 static_ts_offset = 0;
	guint16 version_raw = 0;
	g_autoptr(GBytes) fw_payload = NULL;
	g_autofree gchar *version = NULL;

	/* check signature */
	if (!fu_memcpy_safe(sign,
			    sizeof(sign),
			    0, /* dst */
			    buf,
			    bufsz,
			    GENESYS_USBHUB_FW_SIG_OFFSET,
			    sizeof(sign),
			    error))
		return FALSE;

	if (memcmp(sign, GENESYS_USBHUB_FW_SIG_TEXT_HUB, sizeof(sign)) != 0 &&
	    memcmp(sign, GENESYS_USBHUB_FW_SIG_TEXT_HUB_SIGN, sizeof(sign)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "signature not supported");
		return FALSE;
	}

	/* get chip */
	if (!fu_genesys_usbhub_firmware_get_chip(self, buf, bufsz, error))
		return FALSE;

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
	default:
		break;
	}

	/* get static tool string */
	if (!fu_memcpy_safe((guint8 *)&self->static_ts,
			    sizeof(self->static_ts),
			    0, /* dst */
			    buf,
			    bufsz,
			    static_ts_offset, /* src */
			    sizeof(self->static_ts),
			    error))
		return FALSE;

	/* deduce code size */
	switch (self->chip.model) {
	case ISP_MODEL_HUB_GL3521:
		code_size = 0x5000;
		break;
	case ISP_MODEL_HUB_GL3523: {
		code_size = 0x6000;
		if (self->chip.revision == 50) {
			guint8 kbs = 0;

			if (!fu_common_read_uint8_safe(buf,
						       bufsz,
						       GENESYS_USBHUB_CODE_SIZE_OFFSET,
						       &kbs,
						       error))
				return FALSE;
			code_size = 1024 * kbs;
		}
		break;
	}
	case ISP_MODEL_HUB_GL3590: {
		guint8 kbs = 0;
		if (!fu_common_read_uint8_safe(buf,
					       bufsz,
					       GENESYS_USBHUB_CODE_SIZE_OFFSET,
					       &kbs,
					       error))
			return FALSE;
		code_size = 1024 * kbs;
		break;
	}
	default:
		break;
	}
	fu_firmware_set_size(firmware, code_size);

	/* calculate checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0)
		if (!fu_genesys_usbhub_firmware_verify(buf, bufsz, code_size, error))
			return FALSE;

	/* get firmware version */
	if (!fu_common_read_uint16_safe(buf,
					bufsz,
					GENESYS_USBHUB_VERSION_OFFSET,
					&version_raw,
					G_LITTLE_ENDIAN,
					error))
		return FALSE;
	fu_firmware_set_version_raw(firmware, version_raw);
	version =
	    g_strdup_printf("%02x.%02x", (version_raw & 0xFF00U) >> 8, (version_raw & 0x00FFU));
	fu_firmware_set_version(firmware, version);

	/* success */
	return TRUE;
}

static GBytes *
fu_genesys_usbhub_firmware_write(FuFirmware *firmware, GError **error)
{
	FuGenesysUsbhubFirmware *self = FU_GENESYS_USBHUB_FIRMWARE(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	guint16 code_size = 0x6000;
	guint16 checksum;

	/* fixed size */
	fu_byte_array_set_size(buf, code_size);

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
	if (!fu_memcpy_safe(buf->data,
			    buf->len,
			    GENESYS_USBHUB_STATIC_TOOL_STRING_OFFSET_GL3523, /* dst */
			    (const guint8 *)&self->static_ts,
			    sizeof(self->static_ts),
			    0x0, /* src */
			    sizeof(self->static_ts),
			    error))
		return NULL;

	/* checksum */
	checksum = fu_common_sum16(buf->data, code_size - sizeof(checksum));
	if (!fu_common_write_uint16_safe(buf->data,
					 buf->len,
					 code_size - sizeof(guint16),
					 checksum,
					 G_BIG_ENDIAN,
					 error))
		return NULL;

	/* version */
	if (!fu_common_write_uint16_safe(buf->data,
					 buf->len,
					 GENESYS_USBHUB_VERSION_OFFSET,
					 0x1234, // TODO: parse from firmware version string
					 G_BIG_ENDIAN,
					 error))
		return NULL;

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static gchar *
fu_genesys_usbhub_firmware_get_project_ic_type_string(const gchar *tmp)
{
	return g_strdup_printf("GL%c%c%c%c-%c%c", tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);
}

static void
fu_genesys_usbhub_firmware_export(FuFirmware *firmware,
				  FuFirmwareExportFlags flags,
				  XbBuilderNode *bn)
{
	FuGenesysUsbhubFirmware *self = FU_GENESYS_USBHUB_FIRMWARE(firmware);
	g_autofree gchar *tool_string_version = NULL;
	g_autofree gchar *mask_project_code = NULL;
	g_autofree gchar *mask_project_hardware = NULL;
	g_autofree gchar *mask_project_firmware = NULL;
	g_autofree gchar *mask_project_ic_type = NULL;
	g_autofree gchar *running_project_code = NULL;
	g_autofree gchar *running_project_hardware = NULL;
	g_autofree gchar *running_project_firmware = NULL;
	g_autofree gchar *running_project_ic_type = NULL;

	tool_string_version = fu_common_strsafe((const gchar *)&self->static_ts.tool_string_version,
						sizeof(self->static_ts.tool_string_version));
	fu_xmlb_builder_insert_kv(bn, "tool_string_version", tool_string_version);

	mask_project_code = fu_common_strsafe((const gchar *)&self->static_ts.mask_project_code,
					      sizeof(self->static_ts.mask_project_code));
	fu_xmlb_builder_insert_kv(bn, "mask_project_code", mask_project_code);

	mask_project_hardware =
	    fu_common_strsafe((const gchar *)&self->static_ts.mask_project_hardware,
			      sizeof(self->static_ts.mask_project_hardware));
	if (mask_project_hardware != NULL)
		mask_project_hardware[0] += 0x10; /* '1' -> 'A'... */
	fu_xmlb_builder_insert_kv(bn, "mask_project_hardware", mask_project_hardware);

	mask_project_firmware =
	    fu_common_strsafe((const gchar *)&self->static_ts.mask_project_firmware,
			      sizeof(self->static_ts.mask_project_firmware));
	fu_xmlb_builder_insert_kv(bn, "mask_project_firmware", mask_project_firmware);

	mask_project_ic_type = fu_genesys_usbhub_firmware_get_project_ic_type_string(
	    (const gchar *)self->static_ts.mask_project_ic_type);
	fu_xmlb_builder_insert_kv(bn, "mask_project_ic_type", mask_project_ic_type);

	running_project_code =
	    fu_common_strsafe((const gchar *)&self->static_ts.running_project_code,
			      sizeof(self->static_ts.running_project_code));
	fu_xmlb_builder_insert_kv(bn, "running_project_code", running_project_code);

	running_project_hardware =
	    fu_common_strsafe((const gchar *)&self->static_ts.running_project_hardware,
			      sizeof(self->static_ts.running_project_hardware));
	if (running_project_hardware != NULL)
		running_project_hardware[0] += 0x10; /* '1' -> 'A'... */
	fu_xmlb_builder_insert_kv(bn, "running_project_hardware", running_project_hardware);

	running_project_firmware =
	    fu_common_strsafe((const gchar *)&self->static_ts.running_project_firmware,
			      sizeof(self->static_ts.running_project_firmware));
	fu_xmlb_builder_insert_kv(bn, "running_project_firmware", running_project_firmware);

	running_project_ic_type = fu_genesys_usbhub_firmware_get_project_ic_type_string(
	    (const gchar *)self->static_ts.running_project_ic_type);
	fu_xmlb_builder_insert_kv(bn, "running_project_ic_type", running_project_ic_type);
}

static gboolean
fu_genesys_usbhub_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuGenesysUsbhubFirmware *self = FU_GENESYS_USBHUB_FIRMWARE(firmware);
	const gchar *tmp;
	guint64 tmp64;

	/* optional properties */
	tmp64 = xb_node_query_text_as_uint(n, "tool_string_version", NULL);
	if (tmp64 != G_MAXUINT64) {
		if (tmp64 > G_MAXUINT8) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "invalid tool_string_version");
			return FALSE;
		}
		self->static_ts.tool_string_version = tmp64;
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
		if (!fu_memcpy_safe((guint8 *)&self->static_ts.mask_project_code,
				    sizeof(self->static_ts.mask_project_code),
				    0x0, /* dst */
				    (const guint8 *)tmp,
				    len,
				    0x0, /* src */
				    len,
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
		if (!fu_memcpy_safe((guint8 *)&self->static_ts.mask_project_ic_type,
				    sizeof(self->static_ts.mask_project_ic_type),
				    0x0, /* dst */
				    (const guint8 *)tmp,
				    len,
				    0x0, /* src */
				    len,
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
fu_genesys_usbhub_firmware_class_init(FuGenesysUsbhubFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
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
