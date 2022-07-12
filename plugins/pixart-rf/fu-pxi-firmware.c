/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-pxi-firmware.h"

#define PIXART_RF_FW_HEADER_SIZE       32 /* bytes */
#define PIXART_RF_FW_HEADER_TAG_OFFSET 24

#define PIXART_RF_FW_HEADER_MAGIC 0x55AA55AA55AA55AA

struct _FuPxiFirmware {
	FuFirmware parent_instance;
	gchar *model_name;
};

G_DEFINE_TYPE(FuPxiFirmware, fu_pxi_firmware, FU_TYPE_FIRMWARE)

const gchar *
fu_pxi_firmware_get_model_name(FuPxiFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_FIRMWARE(self), NULL);
	return self->model_name;
}

static void
fu_pxi_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuPxiFirmware *self = FU_PXI_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kv(bn, "model_name", self->model_name);
}

static gboolean
fu_pxi_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	guint64 magic = 0;

	/* is a footer */
	if (!fu_memread_uint64_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    g_bytes_get_size(fw) - PIXART_RF_FW_HEADER_SIZE +
					PIXART_RF_FW_HEADER_TAG_OFFSET,
				    &magic,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	if (magic != PIXART_RF_FW_HEADER_MAGIC) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid magic, expected 0x%08X got 0x%08X",
			    (guint32)PIXART_RF_FW_HEADER_MAGIC,
			    (guint32)magic);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_pxi_firmware_parse(FuFirmware *firmware,
		      GBytes *fw,
		      gsize offset,
		      FwupdInstallFlags flags,
		      GError **error)
{
	FuPxiFirmware *self = FU_PXI_FIRMWARE(firmware);
	const guint8 *buf;
	gsize bufsz = 0;
	guint32 version_raw = 0;
	guint8 fw_header[PIXART_RF_FW_HEADER_SIZE];
	guint8 model_name[FU_PXI_DEVICE_MODEL_NAME_LEN] = {0x0};
	g_autofree gchar *version = NULL;

	/* get fw header from buf */
	buf = g_bytes_get_data(fw, &bufsz);
	if (!fu_memcpy_safe(fw_header,
			    sizeof(fw_header),
			    0x0,
			    buf,
			    bufsz,
			    bufsz - sizeof(fw_header),
			    sizeof(fw_header),
			    error)) {
		g_prefix_error(error, "failed to read fw header: ");
		return FALSE;
	}
	if (g_getenv("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_dump_raw(G_LOG_DOMAIN, "fw header", fw_header, sizeof(fw_header));
	}

	/* set fw version */
	version_raw = (((guint32)(fw_header[0] - '0')) << 16) +
		      (((guint32)(fw_header[2] - '0')) << 8) + (guint32)(fw_header[4] - '0');
	fu_firmware_set_version_raw(firmware, version_raw);
	version = fu_version_from_uint32(version_raw, FWUPD_VERSION_FORMAT_DELL_BIOS);
	fu_firmware_set_version(firmware, version);

	/* set fw model name */
	if (!fu_memcpy_safe(model_name,
			    sizeof(model_name),
			    0x0,
			    fw_header,
			    sizeof(fw_header),
			    0x05,
			    sizeof(model_name),
			    error)) {
		g_prefix_error(error, "failed to get fw model name: ");
		return FALSE;
	}
	self->model_name = g_strndup((gchar *)model_name, sizeof(model_name));

	/* success */
	fu_firmware_set_bytes(firmware, fw);
	return TRUE;
}

static gboolean
fu_pxi_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuPxiFirmware *self = FU_PXI_FIRMWARE(firmware);
	const gchar *tmp;

	/* optional properties */
	tmp = xb_node_query_text(n, "model_name", NULL);
	if (tmp != NULL)
		self->model_name = g_strdup(tmp);

	/* success */
	return TRUE;
}

static GBytes *
fu_pxi_firmware_write(FuFirmware *firmware, GError **error)
{
	FuPxiFirmware *self = FU_PXI_FIRMWARE(firmware);
	guint8 fw_header[PIXART_RF_FW_HEADER_SIZE] = {0x0};
	guint64 version_raw = fu_firmware_get_version_raw(firmware);
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GBytes) blob = NULL;
	const guint8 tag[] = {
	    0x55,
	    0xAA,
	    0x55,
	    0xAA,
	    0x55,
	    0xAA,
	    0x55,
	    0xAA,
	};

	/* data first */
	blob = fu_firmware_get_bytes_with_patches(firmware, error);
	if (blob == NULL)
		return NULL;
	buf = g_byte_array_sized_new(g_bytes_get_size(blob) + sizeof(fw_header));
	fu_byte_array_append_bytes(buf, blob);

	/* footer */
	if (!fu_memcpy_safe(fw_header,
			    sizeof(fw_header),
			    PIXART_RF_FW_HEADER_TAG_OFFSET, /* dst */
			    tag,
			    sizeof(tag),
			    0x0, /* src */
			    sizeof(tag),
			    error))
		return NULL;
	fw_header[0] = ((version_raw >> 16) & 0xff) + '0';
	fw_header[1] = '.';
	fw_header[2] = ((version_raw >> 8) & 0xff) + '0';
	fw_header[3] = '.';
	fw_header[4] = ((version_raw >> 0) & 0xff) + '0';
	if (!g_ascii_isdigit(fw_header[0]) || !g_ascii_isdigit(fw_header[2]) ||
	    !g_ascii_isdigit(fw_header[4])) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "cannot write invalid version number 0x%x",
			    (guint)version_raw);
		return NULL;
	}
	if (self->model_name != NULL) {
		gsize model_namesz = MIN(strlen(self->model_name), FU_PXI_DEVICE_MODEL_NAME_LEN);
		if (!fu_memcpy_safe(fw_header,
				    sizeof(fw_header),
				    0x05, /* dst */
				    (const guint8 *)self->model_name,
				    model_namesz,
				    0x0, /* src */
				    model_namesz,
				    error)) {
			g_prefix_error(error, "failed to get fw model name: ");
			return NULL;
		}
	}

	g_byte_array_append(buf, fw_header, sizeof(fw_header));
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_pxi_firmware_init(FuPxiFirmware *self)
{
}

static void
fu_pxi_firmware_finalize(GObject *object)
{
	FuPxiFirmware *self = FU_PXI_FIRMWARE(object);
	g_free(self->model_name);
	G_OBJECT_CLASS(fu_pxi_firmware_parent_class)->finalize(object);
}

static void
fu_pxi_firmware_class_init(FuPxiFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_pxi_firmware_finalize;
	klass_firmware->check_magic = fu_pxi_firmware_check_magic;
	klass_firmware->parse = fu_pxi_firmware_parse;
	klass_firmware->build = fu_pxi_firmware_build;
	klass_firmware->write = fu_pxi_firmware_write;
	klass_firmware->export = fu_pxi_firmware_export;
}

FuFirmware *
fu_pxi_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_PXI_FIRMWARE, NULL));
}
