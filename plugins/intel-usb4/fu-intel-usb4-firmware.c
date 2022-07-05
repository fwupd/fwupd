/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Intel Corporation.
 * Copyright (C) 2021 Dell Inc.
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
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-intel-usb4-firmware.h"

struct _FuIntelUsb4Firmware {
	FuIntelUsb4NvmClass parent_instance;
};

G_DEFINE_TYPE(FuIntelUsb4Firmware, fu_intel_usb4_firmware, FU_TYPE_INTEL_USB4_NVM)

static gboolean
fu_intel_usb4_firmware_parse(FuFirmware *firmware,
			     GBytes *fw,
			     gsize offset,
			     FwupdInstallFlags flags,
			     GError **error)
{
	guint32 hdr_offset = 0x0;

	/* get header offset */
	if (!fu_memread_uint32_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    offset,
				    &hdr_offset,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	/* FuIntelUsb4Nvm->parse */
	return FU_FIRMWARE_CLASS(fu_intel_usb4_firmware_parent_class)
	    ->parse(firmware, fw, offset + hdr_offset, flags, error);
}

static void
fu_intel_usb4_firmware_init(FuIntelUsb4Firmware *self)
{
}

static void
fu_intel_usb4_firmware_class_init(FuIntelUsb4FirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_intel_usb4_firmware_parse;
}

FuFirmware *
fu_intel_usb4_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_INTEL_USB4_FIRMWARE, NULL));
}
