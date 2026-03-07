/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-lenovo-ldc-firmware.h"
#include "fu-lenovo-ldc-struct.h"

struct _FuLenovoLdcFirmware {
	FuFirmware parent_instance;
	guint16 start_addr;
};

G_DEFINE_TYPE(FuLenovoLdcFirmware, fu_lenovo_ldc_firmware, FU_TYPE_FIRMWARE)

static void
fu_lenovo_ldc_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuLenovoLdcFirmware *self = FU_LENOVO_LDC_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "start_addr", self->start_addr);
}

static gboolean
fu_lenovo_ldc_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuLenovoLdcFirmware *self = FU_LENOVO_LDC_FIRMWARE(firmware);
	guint64 tmp;

	/* TODO: load from .builder.xml */
	tmp = xb_node_query_text_as_uint(n, "start_addr", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->start_addr = tmp;

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_ldc_firmware_validate(FuFirmware *firmware,
				GInputStream *stream,
				gsize offset,
				GError **error)
{
	return fu_struct_lenovo_ldc_hdr_validate_stream(stream, offset, error);
}

static gboolean
fu_lenovo_ldc_firmware_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     FuFirmwareParseFlags flags,
			     GError **error)
{
	FuLenovoLdcFirmware *self = FU_LENOVO_LDC_FIRMWARE(firmware);
	g_autoptr(FuStructLenovoLdcHdr) st_hdr = NULL;

	/* TODO: parse firmware into images */
	st_hdr = fu_struct_lenovo_ldc_hdr_parse_stream(stream, 0x0, error);
	if (st_hdr == NULL)
		return FALSE;
	self->start_addr = 0x1234;
	fu_firmware_set_version(firmware, "1.2.3");
	// fu_firmware_set_bytes(firmware, fw);
	return TRUE;
}

static GByteArray *
fu_lenovo_ldc_firmware_write(FuFirmware *firmware, GError **error)
{
	FuLenovoLdcFirmware *self = FU_LENOVO_LDC_FIRMWARE(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) fw = NULL;

	/* data first */
	fw = fu_firmware_get_bytes_with_patches(firmware, error);
	if (fw == NULL)
		return NULL;

	/* TODO: write to the buffer with the correct format */
	g_assert(self != NULL);
	fu_byte_array_append_bytes(buf, fw);

	/* success */
	return g_steal_pointer(&buf);
}

guint16
fu_lenovo_ldc_firmware_get_start_addr(FuLenovoLdcFirmware *self)
{
	g_return_val_if_fail(FU_IS_LENOVO_LDC_FIRMWARE(self), G_MAXUINT16);
	return self->start_addr;
}

static void
fu_lenovo_ldc_firmware_init(FuLenovoLdcFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_lenovo_ldc_firmware_class_init(FuLenovoLdcFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_lenovo_ldc_firmware_validate;
	firmware_class->parse = fu_lenovo_ldc_firmware_parse;
	firmware_class->write = fu_lenovo_ldc_firmware_write;
	firmware_class->build = fu_lenovo_ldc_firmware_build;
	firmware_class->export = fu_lenovo_ldc_firmware_export;
}

FuFirmware *
fu_lenovo_ldc_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_LENOVO_LDC_FIRMWARE, NULL));
}
