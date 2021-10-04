/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-lenovo-dock-firmware.h"

struct _FuLenovoDockFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuLenovoDockFirmware, fu_lenovo_dock_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_lenovo_dock_firmware_parse(FuFirmware *firmware,
			      GBytes *fw,
			      guint64 addr_start,
			      guint64 addr_end,
			      FwupdInstallFlags flags,
			      GError **error)
{
	// FIXME
	fu_firmware_set_bytes(firmware, fw);
	return TRUE;
}

static void
fu_lenovo_dock_firmware_init(FuLenovoDockFirmware *self)
{
}

static void
fu_lenovo_dock_firmware_class_init(FuLenovoDockFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_lenovo_dock_firmware_parse;
}

FuFirmware *
fu_lenovo_dock_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_LENOVO_DOCK_FIRMWARE, NULL));
}
