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

#include "fu-device.h"
#include "fwupd-error.h"
#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#include "fu-dell-dock-common.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);

	/* allow these to be built by quirks */
	g_type_ensure (FU_TYPE_DELL_DOCK_STATUS);
	g_type_ensure (FU_TYPE_DELL_DOCK_MST);

	/* currently slower performance, but more reliable in corner cases */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_BETTER_THAN, "synaptics_mst");
}

static gboolean
fu_plugin_dell_dock_create_node (FuPlugin *plugin,
				 FuDevice *device,
				 GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	fu_device_set_quirks (device, fu_plugin_get_quirks (plugin));
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	fu_plugin_device_add (plugin, device);

	return TRUE;
}

static gboolean
fu_plugin_dell_dock_probe (FuPlugin *plugin,
			   FuDevice *symbiote,
			   GError **error)
{
	g_autoptr(FuDellDockEc) ec_device = NULL;

	/* create all static endpoints */
	ec_device = fu_dell_dock_ec_new (symbiote);
	if (!fu_plugin_dell_dock_create_node (plugin,
					      FU_DEVICE (ec_device),
					      error))
		return FALSE;

	/* create TBT endpoint if Thunderbolt SKU and Thunderbolt link inactive */
	if (fu_dell_dock_ec_needs_tbt (FU_DEVICE (ec_device))) {
		g_autoptr(FuDellDockTbt) tbt_device = fu_dell_dock_tbt_new ();
		fu_device_add_child (FU_DEVICE (ec_device), FU_DEVICE (tbt_device));
		if (!fu_plugin_dell_dock_create_node (plugin,
						      FU_DEVICE (tbt_device),
						      error))
			return FALSE;
	}

	return TRUE;
}

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin,
			    FuUsbDevice *device,
			    GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuDellDockHub) hub = fu_dell_dock_hub_new (device);
	FuDevice *fu_device = FU_DEVICE (hub);
	const gchar *key = NULL;

	locker = fu_device_locker_new (fu_device, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add (plugin, fu_device);

	if (fu_device_has_custom_flag (fu_device, "has-bridge")) {
		g_autoptr(GError) error_local = NULL;

		/* only add the device with parent to cache */
		key = fu_device_get_id (fu_device);
		if (fu_plugin_cache_lookup (plugin, key) != NULL) {
			g_debug ("Ignoring already added device %s", key);
			return TRUE;
		}
		fu_plugin_cache_add (plugin, key, fu_device);

		/* probe for extended devices */
		if (!fu_plugin_dell_dock_probe (plugin,
						fu_device,
						&error_local)) {
			g_warning ("Failed to probe bridged devices for %s: %s",
				   key,
				   error_local->message);
		}
	}

	/* clear updatable flag if parent doesn't have it */
	fu_dell_dock_clone_updatable (fu_device);

	return TRUE;
}

gboolean
fu_plugin_device_removed (FuPlugin *plugin, FuDevice *device, GError **error)
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
fu_plugin_composite_prepare (FuPlugin *plugin,
			     GPtrArray *devices,
			     GError **error)
{
	FuDevice *parent = fu_plugin_dell_dock_get_ec (devices);
	gboolean remaining_replug = FALSE;

	if (parent == NULL)
		return TRUE;

	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index (devices, i);
		/* if thunderbolt is part of transaction our family is leaving us */
		if (g_strcmp0 (fu_device_get_plugin (dev), "thunderbolt") == 0) {
			if (fu_device_get_parent (dev) != parent)
				continue;
			fu_dell_dock_will_replug (parent);
			/* set all other devices to replug */
			remaining_replug = TRUE;
			continue;
		}
		/* different device */
		if (fu_device_get_parent (dev) != parent)
			continue;
		if (remaining_replug)
			fu_dell_dock_will_replug (dev);
	}

	return TRUE;
}

gboolean
fu_plugin_composite_cleanup (FuPlugin *plugin,
			     GPtrArray *devices,
			     GError **error)
{
	FuDevice *parent = fu_plugin_dell_dock_get_ec (devices);
	g_autoptr(FuDeviceLocker) locker = NULL;

	if (parent == NULL)
		return TRUE;

	locker = fu_device_locker_new (parent, error);
	if (locker == NULL)
		return FALSE;

	return fu_dell_dock_ec_reboot_dock (parent, error);
}
