/*
 * Copyright 2024 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-amd-kria-persistent-firmware.h"
#include "fu-amd-kria-persistent-struct.h"

struct _FuAmdKriaPersistentFirmware {
	FuFirmwareClass parent_instance;
	BootImageId last_booted;
};

G_DEFINE_TYPE(FuAmdKriaPersistentFirmware, fu_amd_kria_persistent_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_amd_kria_persistent_firmware_parse(FuFirmware *firmware,
				      GInputStream *stream,
				      gsize offset,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuAmdKriaPersistentFirmware *self = FU_AMD_KRIA_PERSISTENT_FIRMWARE(firmware);
	g_autoptr(FuStructAmdKriaPersistReg) content = NULL;

	content = fu_struct_amd_kria_persist_reg_parse_stream(stream, offset, error);
	if (content == NULL)
		return FALSE;
	self->last_booted = fu_struct_amd_kria_persist_reg_get_last_booted_img(content);

	return TRUE;
}

gboolean
fu_amd_kria_persistent_firmware_booted_image_a(FuAmdKriaPersistentFirmware *self)
{
	return self->last_booted == BOOT_IMAGE_ID_A;
}

static void
fu_amd_kria_persistent_firmware_init(FuAmdKriaPersistentFirmware *self)
{
}

static void
fu_amd_kria_persistent_firmware_export(FuFirmware *firmware,
				       FuFirmwareExportFlags flags,
				       XbBuilderNode *bn)
{
	FuAmdKriaPersistentFirmware *self = FU_AMD_KRIA_PERSISTENT_FIRMWARE(firmware);

	fu_xmlb_builder_insert_kv(bn,
				  "last_booted",
				  self->last_booted == BOOT_IMAGE_ID_A ? "A" : "B");
}

static void
fu_amd_kria_persistent_firmware_class_init(FuAmdKriaPersistentFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_amd_kria_persistent_firmware_parse;
	firmware_class->export = fu_amd_kria_persistent_firmware_export;
}

FuFirmware *
fu_amd_kria_persistent_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_AMD_KRIA_PERSISTENT_FIRMWARE, NULL));
}
