/*
 * Copyright (C) 2021 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#define BOOT_ORDER_LOCK "com.thinklmi.BootOrderLock"

static gboolean
fu_plugin_lenovo_thinklmi_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);

	if (fu_context_get_bios_setting(ctx, BOOT_ORDER_LOCK) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unable to access needed attribute");
		return FALSE;
	}

	return TRUE;
}

static void
fu_plugin_lenovo_thinklmi_device_registered(FuPlugin *plugin, FuDevice *device)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FwupdBiosSetting *attr;

	if (g_strcmp0(fu_device_get_plugin(device), "uefi_capsule") != 0)
		return;

	/* check if boot order lock is turned on */
	attr = fu_context_get_bios_setting(ctx, BOOT_ORDER_LOCK);
	if (attr == NULL) {
		g_debug("failed to find %s in cache\n", BOOT_ORDER_LOCK);
		return;
	}
	if (g_strcmp0(fwupd_bios_setting_get_current_value(attr), "Enable") == 0) {
		fu_device_inhibit(device,
				  "uefi-capsule-bootorder",
				  "BootOrder is locked in firmware setup");
	}

	/* check if we're pending for a reboot */
	if (fu_context_get_bios_setting_pending_reboot(ctx)) {
		fu_device_inhibit(device,
				  "uefi-capsule-pending-reboot",
				  "UEFI BIOS settings update pending reboot");
	}
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->startup = fu_plugin_lenovo_thinklmi_startup;
	vfuncs->device_registered = fu_plugin_lenovo_thinklmi_device_registered;
}
