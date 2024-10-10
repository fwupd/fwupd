/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-fpc-ff2-firmware.h"
#include "fu-fpc-struct.h"

struct _FuFpcFf2Firmware {
	FuFirmware parent_instance;
	guint32 blocks_num;
};

G_DEFINE_TYPE(FuFpcFf2Firmware, fu_fpc_ff2_firmware, FU_TYPE_FIRMWARE)

static void
fu_fpc_ff2_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuFpcFf2Firmware *self = FU_FPC_FF2_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "blocks_num", self->blocks_num);
}

static gboolean
fu_fpc_ff2_firmware_validate(FuFirmware *firmware,
			     GInputStream *stream,
			     gsize offset,
			     GError **error)
{
	return fu_struct_fpc_ff2_hdr_validate_stream(stream, offset, error);
}

static gboolean
fu_fpc_ff2_firmware_parse(FuFirmware *firmware,
			  GInputStream *stream,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuFpcFf2Firmware *self = FU_FPC_FF2_FIRMWARE(firmware);
	g_autoptr(FuStructFpcFf2Hdr) st_hdr = NULL;

	st_hdr = fu_struct_fpc_ff2_hdr_parse_stream(stream, 0x0, error);
	if (st_hdr == NULL)
		return FALSE;
	self->blocks_num = fu_struct_fpc_ff2_hdr_get_blocks_num(st_hdr);

	/* success */
	return TRUE;
}

guint32
fu_fpc_ff2_firmware_get_blocks_num(FuFpcFf2Firmware *self)
{
	g_return_val_if_fail(FU_IS_FPC_FF2_FIRMWARE(self), G_MAXUINT16);
	return self->blocks_num;
}

static void
fu_fpc_ff2_firmware_init(FuFpcFf2Firmware *self)
{
}

static void
fu_fpc_ff2_firmware_class_init(FuFpcFf2FirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_fpc_ff2_firmware_validate;
	firmware_class->parse = fu_fpc_ff2_firmware_parse;
	firmware_class->export = fu_fpc_ff2_firmware_export;
}
