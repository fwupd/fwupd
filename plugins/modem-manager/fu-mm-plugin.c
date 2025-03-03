/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-backend.h"
#include "fu-mm-device.h"
#include "fu-mm-fastboot-device.h"
#include "fu-mm-firehose-device.h"
#include "fu-mm-mhi-qcdm-device.h"
#include "fu-mm-qcdm-device.h"
#include "fu-mm-dfota-device.h"
#include "fu-mm-fdl-device.h"
#include "fu-mm-mbim-device.h"
#include "fu-mm-qmi-device.h"

#define FU_MODEM_MANAGER_PLUGIN(o) fu_plugin_get_data(FU_PLUGIN(o))

static void
fu_mm_plugin_load(FuContext *ctx)
{
	fu_context_add_quirk_key(ctx, "ModemManagerBranchAtCommand");
}

static gboolean
fu_mm_plugin_backend_device_added(FuPlugin *plugin,
				  FuDevice *device,
				  FuProgress *progress,
				  GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* ignore anything from other backends, e.g. usb */
	if (!FU_IS_MM_DEVICE(device)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return FALSE;
	}

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add(plugin, device);

	/* success */
	return TRUE;
}

static void
fu_mm_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuBackend) backend = fu_mm_backend_new(ctx);

	fu_context_add_backend(ctx, backend);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MM_DEVICE);		/* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MM_DFOTA_DEVICE);	/* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MM_FASTBOOT_DEVICE); /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MM_FDL_DEVICE);	/* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MM_FIREHOSE_DEVICE); /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MM_MBIM_DEVICE);	/* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MM_MHI_QCDM_DEVICE); /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MM_QCDM_DEVICE);	/* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MM_QMI_DEVICE);	/* coverage */
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->load = fu_mm_plugin_load;
	vfuncs->constructed = fu_mm_plugin_constructed;
	vfuncs->backend_device_added = fu_mm_plugin_backend_device_added;
}
