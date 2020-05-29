/*
 * Copyright (C) 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-fmap-firmware.h"

struct _FuFmapFirmware {
	FuFirmware		 parent_instance;
};

G_DEFINE_TYPE (FuFmapFirmware, fu_fmap_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_fmap_firmware_parse (FuFirmware *firmware,
			GBytes *fw,
			guint64 addr_start,
			guint64 addr_end,
			FwupdInstallFlags flags,
			GError **error)
{
	/* set bogus version */
	fu_firmware_set_version (firmware, "1.2.3");

	/* success */
	return TRUE;
}

static void
fu_fmap_firmware_init (FuFmapFirmware *self)
{
}

static void
fu_fmap_firmware_class_init (FuFmapFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_fmap_firmware_parse;
}

/**
 * fu_fmap_firmware_new
 *
 * Creates a new #FuFirmware of sub type fmap
 *
 * Since: 1.5.0
 **/
FuFirmware *
fu_fmap_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_FMAP_FIRMWARE, NULL));
}
