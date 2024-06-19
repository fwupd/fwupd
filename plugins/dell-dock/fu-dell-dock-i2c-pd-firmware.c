/*
 * Copyright 2024 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-dock-i2c-pd-firmware.h"

#define DOCK_PD_VERSION_OFFSET 0x46
#define DOCK_PD_VERSION_MAGIC  0x00770064

struct _FuDellDockPdFirmware {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuDellDockPdFirmware, fu_dell_dock_pd_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_dell_dock_pd_firmware_parse(FuFirmware *firmware,
			       GInputStream *stream,
			       gsize offset,
			       FwupdInstallFlags flags,
			       GError **error)
{
	guint32 ver_tmp;
	/* find loc for the magic */

	/* read version from firmware */
	if (!fu_input_stream_read_u32(stream, offset, &ver_tmp, G_LITTLE_ENDIAN, error))
		return FALSE;

	return TRUE;
}

static void
fu_dell_dock_pd_firmware_init(FuDellDockPdFirmware *self)
{
}

static void
fu_dell_dock_pd_firmware_class_init(FuDellDockPdFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_dell_dock_pd_firmware_parse;
}

FuFirmware *
fu_dell_dock_pd_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_DELL_DOCK_PD_FIRMWARE, NULL));
}
