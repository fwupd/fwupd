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

static void
fu_plugin_dell_dock_init(FuPlugin *plugin)
{
	FuContext *ctx = fu_plugin_get_context(plugin);

	fu_context_add_quirk_key(ctx, "DellDockBlobBuildOffset");
	fu_context_add_quirk_key(ctx, "DellDockBlobMajorOffset");
	fu_context_add_quirk_key(ctx, "DellDockBlobMinorOffset");
	fu_context_add_quirk_key(ctx, "DellDockBlobVersionOffset");
	fu_context_add_quirk_key(ctx, "DellDockBoardMin");
	fu_context_add_quirk_key(ctx, "DellDockHubVersionLowest");
	fu_context_add_quirk_key(ctx, "DellDockInstallDurationI2C");
	fu_context_add_quirk_key(ctx, "DellDockUnlockTarget");
	fu_context_add_quirk_key(ctx, "DellDockVersionLowest");

	/* allow these to be built by quirks */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_DOCK_STATUS);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_DOCK_MST);

#ifndef _WIN32
	/* currently slower performance, but more reliable in corner cases */
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_BETTER_THAN, "synaptics_mst");
#endif
}

static gboolean
fu_plugin_dell_dock_create_node(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuDeviceLocker) locker = NULL;

	fu_device_set_context(device, ctx);
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	fu_plugin_device_add(plugin, device);

	return TRUE;
}

static gboolean
fu_plugin_dell_dock_probe(FuPlugin *plugin, FuDevice *proxy, GError **error)
{
	const gchar *instance;
	g_autoptr(FuDellDockEc) ec_device = NULL;
	g_autoptr(FuDellDockMst) mst_device = NULL;
	g_autoptr(FuDellDockStatus) status_device = NULL;
	FuContext *ctx = fu_plugin_get_context(plugin);

	/* create ec endpoint */
	ec_device = fu_dell_dock_ec_new(proxy);
	if (!fu_plugin_dell_dock_create_node(plugin, FU_DEVICE(ec_device), error))
		return FALSE;

	/* create mst endpoint */
	mst_device = fu_dell_dock_mst_new();
	if (fu_dell_dock_get_ec_type(FU_DEVICE(ec_device)) == ATOMIC_BASE)
		instance = DELL_DOCK_VMM6210_INSTANCE_ID;
	else
		instance = DELL_DOCK_VM5331_INSTANCE_ID;
	fu_device_set_context(FU_DEVICE(mst_device), ctx);
	fu_device_add_guid(FU_DEVICE(mst_device), fwupd_guid_hash_string(instance));
	fu_device_add_child(FU_DEVICE(ec_device), FU_DEVICE(mst_device));
	fu_device_add_instance_id(FU_DEVICE(mst_device), instance);
	if (!fu_plugin_dell_dock_create_node(plugin, FU_DEVICE(mst_device), error))
		return FALSE;

	/* create package version endpoint */
	status_device = fu_dell_dock_status_new();
	if (fu_dell_dock_get_ec_type(FU_DEVICE(ec_device)) == ATOMIC_BASE)
		instance = DELL_DOCK_ATOMIC_STATUS_INSTANCE_ID;
	else if (fu_dell_dock_module_is_usb4(FU_DEVICE(ec_device)))
		instance = DELL_DOCK_DOCK2_INSTANCE_ID;
	else
		instance = DELL_DOCK_DOCK1_INSTANCE_ID;
	fu_device_set_context(FU_DEVICE(status_device), ctx);
	fu_device_add_guid(FU_DEVICE(status_device), fwupd_guid_hash_string(instance));
	fu_device_add_child(FU_DEVICE(ec_device), FU_DEVICE(status_device));
	fu_device_add_instance_id(FU_DEVICE(status_device), instance);
	if (!fu_plugin_dell_dock_create_node(plugin, FU_DEVICE(status_device), error))
		return FALSE;

	/* create TBT endpoint if Thunderbolt SKU and Thunderbolt link inactive */
	if (fu_dell_dock_ec_needs_tbt(FU_DEVICE(ec_device))) {
		g_autoptr(FuDellDockTbt) tbt_device = fu_dell_dock_tbt_new(proxy);
		fu_device_add_child(FU_DEVICE(ec_device), FU_DEVICE(tbt_device));
		if (!fu_plugin_dell_dock_create_node(plugin, FU_DEVICE(tbt_device), error))
			return FALSE;
	}

	return TRUE;
}

/* prefer to use EC if in the transaction and parent if it is not */
static FuDevice *
fu_plugin_dell_dock_get_ec(GPtrArray *devices)
{
	FuDevice *ec_parent = NULL;
	for (gint i = devices->len - 1; i >= 0; i--) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		FuDevice *parent;
		if (FU_IS_DELL_DOCK_EC(dev))
			return dev;
		parent = fu_device_get_parent(dev);
		if (parent != NULL && FU_IS_DELL_DOCK_EC(parent))
			ec_parent = parent;
	}

	return ec_parent;
}

static gboolean
fu_plugin_dell_dock_backend_device_added(FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuDellDockHub) hub = NULL;
	const gchar *key = NULL;
	GPtrArray *devices;
	FuDevice *ec_device;
	guint device_vid;
	guint device_pid;

	/* not interesting */
	if (!FU_IS_USB_DEVICE(device))
		return TRUE;

	device_vid = (guint)fu_usb_device_get_vid(FU_USB_DEVICE(device));
	device_pid = (guint)fu_usb_device_get_pid(FU_USB_DEVICE(device));
	g_debug("%s: processing usb device, vid: 0x%x, pid: 0x%x",
		fu_plugin_get_name(plugin),
		device_vid,
		device_pid);

	/* GR controller internal USB HUB */
	if (device_vid == GR_USB_VID && device_pid == GR_USB_PID) {
		g_autoptr(FuDellDockUsb4) usb4_dev = NULL;
		usb4_dev = fu_dell_dock_usb4_new(FU_USB_DEVICE(device));
		locker = fu_device_locker_new(FU_DEVICE(usb4_dev), error);
		if (locker == NULL)
			return FALSE;
		fu_plugin_device_add(plugin, FU_DEVICE(usb4_dev));
		return TRUE;
	}

	hub = fu_dell_dock_hub_new(FU_USB_DEVICE(device));
	locker = fu_device_locker_new(FU_DEVICE(hub), error);
	if (locker == NULL)
		return FALSE;

	if (fu_device_has_private_flag(FU_DEVICE(hub), FU_DELL_DOCK_HUB_FLAG_HAS_BRIDGE)) {
		/* only add the device with parent to cache */
		key = fu_device_get_id(FU_DEVICE(hub));
		if (fu_plugin_cache_lookup(plugin, key) != NULL) {
			g_debug("Ignoring already added device %s", key);
			return TRUE;
		}
		/* probe for extended devices */
		if (!fu_plugin_dell_dock_probe(plugin, FU_DEVICE(hub), error))
			return FALSE;
		fu_plugin_cache_add(plugin, key, FU_DEVICE(hub));
	}

	/* add hub instance id after ec probed */
	devices = fu_plugin_get_devices(plugin);
	ec_device = fu_plugin_dell_dock_get_ec(devices);
	if (ec_device != NULL) {
		guint8 ec_type = fu_dell_dock_get_ec_type(ec_device);
		fu_dell_dock_hub_add_instance(FU_DEVICE(hub), ec_type);
	}
	fu_plugin_device_add(plugin, FU_DEVICE(hub));
	return TRUE;
}

static void
fu_plugin_dell_dock_separate_activation(FuPlugin *plugin)
{
	GPtrArray *devices = fu_plugin_get_devices(plugin);
	FuDevice *device_ec = NULL;
	FuDevice *device_usb4 = NULL;

	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device_tmp = g_ptr_array_index(devices, i);
		if (FU_IS_DELL_DOCK_EC(device_tmp))
			device_ec = device_tmp;
		else if (FU_IS_DELL_DOCK_USB4(device_tmp))
			device_usb4 = device_tmp;
	}
	/* both usb4 and ec device are found */
	if (device_usb4 && device_ec) {
		if (fu_device_has_flag(device_usb4, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION) &&
		    fu_device_has_flag(device_ec, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
			fu_device_remove_flag(FU_DEVICE(device_ec),
					      FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
			g_debug("activate for %s is inhibited by %s",
				fu_device_get_name(device_ec),
				fu_device_get_name(device_usb4));
		}
	}
}

static void
fu_plugin_dell_dock_device_registered(FuPlugin *plugin, FuDevice *device)
{
	/* usb4 device from thunderbolt plugin */
	if (g_strcmp0(fu_device_get_plugin(device), "thunderbolt") == 0 &&
	    fu_device_has_guid(device, DELL_DOCK_USB4_INSTANCE_ID)) {
		g_autofree gchar *msg = NULL;
		msg = g_strdup_printf("firmware update inhibited by [%s] plugin",
				      fu_plugin_get_name(plugin));
		fu_device_inhibit(device, "usb4-blocked", msg);
		return;
	}

	/* online activation is mutually exclusive between usb4 and ec */
	if (g_strcmp0(fu_device_get_plugin(device), "dell_dock") == 0 &&
	    (FU_IS_DELL_DOCK_EC(device) || FU_IS_DELL_DOCK_USB4(device)))
		fu_plugin_dell_dock_separate_activation(plugin);
}

static gboolean
fu_plugin_dell_dock_backend_device_removed(FuPlugin *plugin, FuDevice *device, GError **error)
{
	const gchar *device_key = fu_device_get_id(device);
	FuDevice *dev;
	FuDevice *parent;

	/* only the device with bridge will be in cache */
	dev = fu_plugin_cache_lookup(plugin, device_key);
	if (dev == NULL)
		return TRUE;
	fu_plugin_cache_remove(plugin, device_key);

	/* find the parent and ask daemon to remove whole chain  */
	parent = fu_device_get_parent(dev);
	if (parent != NULL && FU_IS_DELL_DOCK_EC(parent)) {
		g_debug("Removing %s (%s)", fu_device_get_name(parent), fu_device_get_id(parent));
		fu_plugin_device_remove(plugin, parent);
	}

	return TRUE;
}

static gboolean
fu_plugin_dell_dock_composite_prepare(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuDevice *parent = fu_plugin_dell_dock_get_ec(devices);
	const gchar *sku;
	if (parent == NULL)
		return TRUE;
	sku = fu_dell_dock_ec_get_module_type(parent);
	if (sku != NULL)
		fu_plugin_add_report_metadata(plugin, "DellDockSKU", sku);

	return TRUE;
}

static gboolean
fu_plugin_dell_dock_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuDevice *parent = fu_plugin_dell_dock_get_ec(devices);
	FuDevice *dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	gboolean needs_activation = FALSE;

	if (parent == NULL)
		return TRUE;

	/* if thunderbolt is in the transaction it needs to be activated separately */
	for (guint i = 0; i < devices->len; i++) {
		dev = g_ptr_array_index(devices, i);
		if ((g_strcmp0(fu_device_get_plugin(dev), "thunderbolt") == 0 ||
		     g_strcmp0(fu_device_get_plugin(dev), "dell_dock") == 0) &&
		    fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
			/* the kernel and/or thunderbolt plugin have been configured to let HW
			 * finish the update */
			if (fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE)) {
				fu_dell_dock_ec_tbt_passive(parent);
				/* run the update immediately - no kernel support */
			} else {
				needs_activation = TRUE;
				break;
			}
		}
	}
	/* separate activation flag between usb4 and ec device */
	fu_plugin_dell_dock_separate_activation(plugin);

	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;

	if (!fu_dell_dock_ec_reboot_dock(parent, error))
		return FALSE;

	/* close this first so we don't have an error from the thunderbolt activation */
	if (!fu_device_locker_close(locker, error))
		return FALSE;

	if (needs_activation && dev != NULL) {
		g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
		if (!fu_device_activate(dev, progress, error))
			return FALSE;
	}

	return TRUE;
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_dell_dock_init;
	vfuncs->device_registered = fu_plugin_dell_dock_device_registered;
	vfuncs->backend_device_added = fu_plugin_dell_dock_backend_device_added;
	vfuncs->backend_device_removed = fu_plugin_dell_dock_backend_device_removed;
	vfuncs->composite_cleanup = fu_plugin_dell_dock_composite_cleanup;
	vfuncs->composite_prepare = fu_plugin_dell_dock_composite_prepare;
}
