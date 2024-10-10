/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-dfu-csr-firmware.h"
#include "fu-dfu-csr-struct.h"

struct _FuDfuCsrFirmware {
	FuFirmware parent_instance;
	guint32 total_sz;
};

G_DEFINE_TYPE(FuDfuCsrFirmware, fu_dfu_csr_firmware, FU_TYPE_FIRMWARE)

static void
fu_dfu_csr_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuDfuCsrFirmware *self = FU_DFU_CSR_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "total_sz", self->total_sz);
}

static gboolean
fu_dfu_csr_firmware_validate(FuFirmware *firmware,
			     GInputStream *stream,
			     gsize offset,
			     GError **error)
{
	return fu_struct_dfu_csr_file_validate_stream(stream, offset, error);
}

static gboolean
fu_dfu_csr_firmware_parse(FuFirmware *firmware,
			  GInputStream *stream,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuDfuCsrFirmware *self = FU_DFU_CSR_FIRMWARE(firmware);
	g_autoptr(GByteArray) st_hdr = NULL;

	/* parse file header */
	st_hdr = fu_struct_dfu_csr_file_parse_stream(stream, 0x0, error);
	if (st_hdr == NULL)
		return FALSE;
	self->total_sz = fu_struct_dfu_csr_file_get_file_len(st_hdr);
	return TRUE;
}

guint32
fu_dfu_csr_firmware_get_total_sz(FuDfuCsrFirmware *self)
{
	g_return_val_if_fail(FU_IS_DFU_CSR_FIRMWARE(self), G_MAXUINT16);
	return self->total_sz;
}

static void
fu_dfu_csr_firmware_init(FuDfuCsrFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
}

static void
fu_dfu_csr_firmware_class_init(FuDfuCsrFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_dfu_csr_firmware_validate;
	firmware_class->parse = fu_dfu_csr_firmware_parse;
	firmware_class->export = fu_dfu_csr_firmware_export;
}

FuFirmware *
fu_dfu_csr_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_DFU_CSR_FIRMWARE, NULL));
}
