/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-efivar.h"
#include "fu-hash.h"
#include "fu-efi-signature-common.h"
#include "fu-efi-signature-parser.h"
#include "fu-uefi-dbx-common.h"
#include "fu-uefi-dbx-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "uefi");
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	g_autoptr(FuUefiDbxDevice) device = fu_uefi_dbx_device_new ();
	if (!fu_device_probe (FU_DEVICE (device), error))
		return FALSE;
	if (!fu_device_setup (FU_DEVICE (device), error))
		return FALSE;
	fu_plugin_device_add (plugin, FU_DEVICE (device));
	return TRUE;
}
