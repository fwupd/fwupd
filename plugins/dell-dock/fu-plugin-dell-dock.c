/*
 * Copyright (C) 2018 Dell Inc.
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

#include "fu-dell-dock-common.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context (plugin);

	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_context_add_quirk_key (ctx, "DellDockBlobBuildOffset");
	fu_context_add_quirk_key (ctx, "DellDockBlobMajorOffset");
	fu_context_add_quirk_key (ctx, "DellDockBlobMinorOffset");
	fu_context_add_quirk_key (ctx, "DellDockBlobVersionOffset");
	fu_context_add_quirk_key (ctx, "DellDockBoardMin");
	fu_context_add_quirk_key (ctx, "DellDockHubVersionLowest");
	fu_context_add_quirk_key (ctx, "DellDockInstallDurationI2C");
	fu_context_add_quirk_key (ctx, "DellDockUnlockTarget");
	fu_context_add_quirk_key (ctx, "DellDockVersionLowest");

	/* allow these to be built by quirks */
	fu_plugin_add_device_gtype (plugin, FU_TYPE_DELL_DOCK_STATUS);
	fu_plugin_add_device_gtype (plugin, FU_TYPE_DELL_DOCK_MST);

	/* currently slower performance, but more reliable in corner cases */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_BETTER_THAN, "synaptics_mst");
}

static gboolean
fu_plugin_dell_dock_create_node (FuPlugin *plugin,
				 FuDevice *device,
				 GError **error)
{
	FuContext *ctx = fu_plugin_get_context (plugin);
	g_autoptr(FuDeviceLocker) locker = NULL;

	fu_device_set_context (device, ctx);
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	fu_plugin_device_add (plugin, device);

	return TRUE;
}

static gboolean
fu_plugin_dell_dock_probe (FuPlugin *plugin,
			   FuDevice *proxy,
			   GError **error)
{
	const gchar* instance;
	g_autoptr(FuDellDockEc) ec_device = NULL;
	g_autoptr(FuDellDockMst) mst_device = NULL;
	g_autoptr(FuDellDockStatus) status_device = NULL;

	/* create ec endpoint */
	ec_device = fu_dell_dock_ec_new (proxy);
	if (!fu_plugin_dell_dock_create_node (plugin,
					      FU_DEVICE (ec_device),
					      error))
		return FALSE;

	/* create mst endpoint */
	mst_device = fu_dell_dock_mst_new ();
	fu_device_add_child (FU_DEVICE (ec_device), FU_DEVICE (mst_device));
	fu_device_add_instance_id (FU_DEVICE (mst_device), "MST-panamera-vmm5331-259");
	if (!fu_plugin_dell_dock_create_node (plugin,
					      FU_DEVICE (mst_device),
					      error))
		return FALSE;

	/* create package version endpoint */
	status_device = fu_dell_dock_status_new ();
	if (fu_dell_dock_module_is_usb4 (FU_DEVICE (ec_device)))
		instance = "USB\\VID_413C&PID_B06E&hub&salomon_mlk_status";
	else
		instance = "USB\\VID_413C&PID_B06E&hub&status";
	fu_device_add_child (FU_DEVICE (ec_device), FU_DEVICE (status_device));
	fu_device_add_instance_id (FU_DEVICE (status_device), instance);
	if (!fu_plugin_dell_dock_create_node (plugin,
					      FU_DEVICE (status_device),
					      error))
		return FALSE;

	/* create TBT endpoint if Thunderbolt SKU and Thunderbolt link inactive */
	if (fu_dell_dock_ec_needs_tbt (FU_DEVICE (ec_device))) {
		g_autoptr(FuDellDockTbt) tbt_device = fu_dell_dock_tbt_new (proxy);
		fu_device_add_child (FU_DEVICE (ec_device), FU_DEVICE (tbt_device));
		if (!fu_plugin_dell_dock_create_node (plugin,
						      FU_DEVICE (tbt_device),
						      error))
			return FALSE;
	}

	return TRUE;
}

gboolean
fu_plugin_backend_device_added (FuPlugin *plugin,
				 FuDevice *device,
				 GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuDellDockHub) hub = NULL;
	const gchar *key = NULL;

	/* not interesting */
	if (!FU_IS_USB_DEVICE (device))
		return TRUE;

	hub = fu_dell_dock_hub_new (FU_USB_DEVICE (device));
	locker = fu_device_locker_new (FU_DEVICE (hub), error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add (plugin, FU_DEVICE (hub));

	if (fu_device_has_custom_flag (FU_DEVICE (hub), "has-bridge")) {
		g_autoptr(GError) error_local = NULL;

		/* only add the device with parent to cache */
		key = fu_device_get_id (FU_DEVICE (hub));
		if (fu_plugin_cache_lookup (plugin, key) != NULL) {
			g_debug ("Ignoring already added device %s", key);
			return TRUE;
		}
		fu_plugin_cache_add (plugin, key, FU_DEVICE (hub));

		/* probe for extended devices */
		if (!fu_plugin_dell_dock_probe (plugin,
						FU_DEVICE (hub),
						&error_local)) {
			g_warning ("Failed to probe bridged devices for %s: %s",
				   key,
				   error_local->message);
		}
	}

	/* clear updatable flag if parent doesn't have it */
	fu_dell_dock_clone_updatable (FU_DEVICE (hub));

	return TRUE;
}

void
fu_plugin_device_registered (FuPlugin *plugin, FuDevice *device)
{
	/* thunderbolt plugin */
	if (g_strcmp0 (fu_device_get_plugin (device), "thunderbolt") != 0 ||
	    fu_device_has_flag (device, FWUPD_DEVICE_FLAG_INTERNAL))
		return;
	/* clone updatable flag to leave in needs activation state */
	fu_dell_dock_clone_updatable (device);
}

gboolean
fu_plugin_backend_device_removed (FuPlugin *plugin, FuDevice *device, GError **error)
{
	const gchar *device_key = fu_device_get_id (device);
	FuDevice *dev;
	FuDevice *parent;

	/* only the device with bridge will be in cache */
	dev = fu_plugin_cache_lookup (plugin, device_key);
	if (dev == NULL)
		return TRUE;
	fu_plugin_cache_remove (plugin, device_key);

	/* find the parent and ask daemon to remove whole chain  */
	parent = fu_device_get_parent (dev);
	if (parent != NULL && FU_IS_DELL_DOCK_EC (parent)) {
		g_debug ("Removing %s (%s)",
			 fu_device_get_name (parent),
			 fu_device_get_id (parent));
		fu_plugin_device_remove (plugin, parent);
	}

	return TRUE;
}

/* prefer to use EC if in the transaction and parent if it is not */
static FuDevice *
fu_plugin_dell_dock_get_ec (GPtrArray *devices)
{
	FuDevice *ec_parent = NULL;
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index (devices, i);
		FuDevice *parent;
		if (FU_IS_DELL_DOCK_EC (dev))
			return dev;
		parent = fu_device_get_parent (dev);
		if (parent != NULL && FU_IS_DELL_DOCK_EC (parent))
			ec_parent = parent;
	}

	return ec_parent;
}

gboolean
fu_plugin_composite_prepare (FuPlugin *plugin, GPtrArray *devices,
			     GError **error)
{
	FuDevice *parent = fu_plugin_dell_dock_get_ec (devices);
	const gchar *sku;
	if (parent == NULL)
		return TRUE;
	sku = fu_dell_dock_ec_get_module_type (parent);
	if (sku != NULL)
		fu_plugin_add_report_metadata (plugin, "DellDockSKU", sku);

	return TRUE;
}

gboolean
fu_plugin_composite_cleanup (FuPlugin *plugin,
			     GPtrArray *devices,
			     GError **error)
{
	FuDevice *parent = fu_plugin_dell_dock_get_ec (devices);
	FuDevice *dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	gboolean needs_activation = FALSE;

	if (parent == NULL)
		return TRUE;

	/* if thunderbolt is in the transaction it needs to be activated separately */
	for (guint i = 0; i < devices->len; i++) {
		dev = g_ptr_array_index (devices, i);
		if (g_strcmp0 (fu_device_get_plugin (dev), "thunderbolt") == 0 &&
		    fu_device_has_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
			/* the kernel and/or thunderbolt plugin have been configured to let HW finish the update */
			if (fu_device_has_flag (dev, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE)) {
				fu_dell_dock_ec_tbt_passive (parent);
			/* run the update immediately - no kernel support */
			} else {
				needs_activation = TRUE;
				break;
			}
		}
	}

	locker = fu_device_locker_new (parent, error);
	if (locker == NULL)
		return FALSE;

	if (!fu_dell_dock_ec_reboot_dock (parent, error))
		return FALSE;

	/* close this first so we don't have an error from the thunderbolt activation */
	if (!fu_device_locker_close (locker, error))
		return FALSE;

	if (needs_activation && dev != NULL) {
		if (!fu_device_activate (dev, error))
			return FALSE;
	}

	return TRUE;
}
