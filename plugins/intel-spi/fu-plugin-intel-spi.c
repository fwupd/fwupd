/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-intel-spi-device.h"

#include "fu-ifd-firmware.h"
#include "fu-ifd-bios.h"

#include "fu-efi-firmware-file.h"
#include "fu-efi-firmware-filesystem.h"
#include "fu-efi-firmware-section.h"
#include "fu-efi-firmware-volume.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_EFI_FIRMWARE_FILE);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_EFI_FIRMWARE_FILESYSTEM);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_EFI_FIRMWARE_SECTION);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_EFI_FIRMWARE_VOLUME);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_IFD_BIOS);
	fu_plugin_add_firmware_gtype (plugin, NULL, FU_TYPE_IFD_FIRMWARE);
	fu_context_add_udev_subsystem (ctx, "pci");
	fu_context_add_quirk_key (ctx, "IntelSpiKind");
	fu_context_add_quirk_key (ctx, "IntelSpiBar");
	fu_context_add_quirk_key (ctx, "IntelSpiBarProxy");
	fu_context_add_quirk_key (ctx, "IntelSpiBiosCntl");
	fu_plugin_add_device_gtype (plugin, FU_TYPE_INTEL_SPI_DEVICE);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	if (fu_common_kernel_locked_down ()) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported when kernel locked down");
		return FALSE;
	}
	return TRUE;
}
