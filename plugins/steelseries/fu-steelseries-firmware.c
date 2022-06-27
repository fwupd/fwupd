/*
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-steelseries-firmware.h"

struct _FuSteelseriesFirmware {
	FuFirmwareClass parent_instance;
	guint32 checksum;
};

G_DEFINE_TYPE(FuSteelseriesFirmware, fu_steelseries_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_steelseries_firmware_parse(FuFirmware *firmware,
			      GBytes *fw,
			      gsize offset,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuSteelseriesFirmware *self = FU_STEELSERIES_FIRMWARE(firmware);
	guint32 checksum_tmp;
	guint32 checksum;

	if (!fu_memread_uint32_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    g_bytes_get_size(fw) - sizeof(checksum),
				    &checksum,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	checksum_tmp =
	    fu_crc32(g_bytes_get_data(fw, NULL), g_bytes_get_size(fw) - sizeof(checksum_tmp));
	if (checksum_tmp != checksum) {
		if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "checksum mismatch, got 0x%08x, expected 0x%08x",
				    checksum_tmp,
				    checksum);
			return FALSE;
		}
		g_debug("ignoring checksum mismatch, got 0x%08x, expected 0x%08x",
			checksum_tmp,
			checksum);
	}

	self->checksum = checksum;
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_set_bytes(firmware, fw);

	/* success */
	return TRUE;
}

static void
fu_steelseries_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuSteelseriesFirmware *self = FU_STEELSERIES_FIRMWARE(firmware);

	fu_xmlb_builder_insert_kx(bn, "checksum", self->checksum);
}

static void
fu_steelseries_firmware_init(FuSteelseriesFirmware *self)
{
}

static void
fu_steelseries_firmware_class_init(FuSteelseriesFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_steelseries_firmware_parse;
	klass_firmware->export = fu_steelseries_firmware_export;
}

FuFirmware *
fu_steelseries_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_STEELSERIES_FIRMWARE, NULL));
}
