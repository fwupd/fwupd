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

#include "fu-dell-dock-common.h"
#include "fu-dell-dock-plugin.h"

struct _FuDellDockPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuDellDockPlugin, fu_dell_dock_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_dell_dock_plugin_create_node(FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	fu_plugin_device_add(plugin, device);

	return TRUE;
}

static gboolean
fu_dell_dock_plugin_probe(FuPlugin *plugin, FuDevice *proxy, GError **error)
{
	const gchar *instance_id_mst;
	const gchar *instance_id_status;
	g_autofree const gchar *instance_guid_mst = NULL;
	g_autofree const gchar *instance_guid_status = NULL;
	g_autoptr(FuDellDockEc) ec_device = NULL;
	g_autoptr(FuDellDockMst) mst_device = NULL;
	g_autoptr(FuDellDockStatus) status_device = NULL;
	FuContext *ctx = fu_plugin_get_context(plugin);

	/* create ec endpoint */
	ec_device = fu_dell_dock_ec_new(proxy);
	if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(ec_device), error))
		return FALSE;

	/* create mst endpoint */
	mst_device = fu_dell_dock_mst_new(ctx);
	if (fu_dell_dock_get_dock_type(FU_DEVICE(ec_device)) == DOCK_BASE_TYPE_ATOMIC)
		instance_id_mst = DELL_DOCK_VMM6210_INSTANCE_ID;
	else
		instance_id_mst = DELL_DOCK_VM5331_INSTANCE_ID;
	fu_device_add_instance_id(FU_DEVICE(mst_device), instance_id_mst);
	instance_guid_mst = fwupd_guid_hash_string(instance_id_mst);
	fu_device_add_guid(FU_DEVICE(mst_device), instance_guid_mst);
	if (!fu_device_probe(FU_DEVICE(mst_device), error))
		return FALSE;
	fu_device_add_child(FU_DEVICE(ec_device), FU_DEVICE(mst_device));
	if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(mst_device), error))
		return FALSE;

	/* create package version endpoint */
	status_device = fu_dell_dock_status_new(ctx);
	if (fu_dell_dock_get_dock_type(FU_DEVICE(ec_device)) == DOCK_BASE_TYPE_ATOMIC)
		instance_id_status = DELL_DOCK_ATOMIC_STATUS_INSTANCE_ID;
	else if (fu_dell_dock_module_is_usb4(FU_DEVICE(ec_device)))
		instance_id_status = DELL_DOCK_DOCK2_INSTANCE_ID;
	else
		instance_id_status = DELL_DOCK_DOCK1_INSTANCE_ID;
	instance_guid_status = fwupd_guid_hash_string(instance_id_status);
	fu_device_add_guid(FU_DEVICE(status_device), fwupd_guid_hash_string(instance_guid_status));
	fu_device_add_child(FU_DEVICE(ec_device), FU_DEVICE(status_device));
	fu_device_add_instance_id(FU_DEVICE(status_device), instance_id_status);
	if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(status_device), error))
		return FALSE;

	/* create TBT endpoint if Thunderbolt SKU and Thunderbolt link inactive */
	if (fu_dell_dock_ec_needs_tbt(FU_DEVICE(ec_device))) {
		g_autoptr(FuDellDockTbt) tbt_device = fu_dell_dock_tbt_new(proxy);
		g_autofree const gchar *instance_guid_tbt =
		    fwupd_guid_hash_string(DELL_DOCK_TBT_INSTANCE_ID);
		fu_device_add_guid(FU_DEVICE(tbt_device), instance_guid_tbt);
		fu_device_add_child(FU_DEVICE(ec_device), FU_DEVICE(tbt_device));
		if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(tbt_device), error))
			return FALSE;
	}

	return TRUE;
}

/* prefer to use EC if in the transaction and parent if it is not */
static FuDevice *
fu_dell_dock_plugin_get_ec(GPtrArray *devices)
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
fu_dell_dock_plugin_backend_device_added(FuPlugin *plugin,
					 FuDevice *device,
					 FuProgress *progress,
					 GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuDellDockHub) hub = NULL;
	const gchar *hub_cache_key = "hub-usb3-gen1";
	GPtrArray *devices;
	FuDevice *ec_device;
	FuDevice *hub_dev;
	guint8 dock_type;

	/* not interesting */
	if (!FU_IS_USB_DEVICE(device))
		return TRUE;

	hub = fu_dell_dock_hub_new(FU_USB_DEVICE(device));
	locker = fu_device_locker_new(FU_DEVICE(hub), error);
	if (locker == NULL)
		return FALSE;

	/* probe extend devices under Usb3.1 Gen 2 Hub */
	if (fu_device_has_private_flag(FU_DEVICE(hub), FU_DELL_DOCK_HUB_FLAG_HAS_BRIDGE)) {
		if (!fu_dell_dock_plugin_probe(plugin, FU_DEVICE(hub), error))
			return FALSE;
	}

	/* process hub devices if ec device is added */
	devices = fu_plugin_get_devices(plugin);
	ec_device = fu_dell_dock_plugin_get_ec(devices);
	if (ec_device == NULL) {
		fu_plugin_cache_add(plugin, hub_cache_key, FU_DEVICE(hub));
		return TRUE;
	}

	/* determine dock type by ec */
	dock_type = fu_dell_dock_get_dock_type(ec_device);
	if (dock_type == DOCK_BASE_TYPE_UNKNOWN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "can't read base dock type from EC");
		return FALSE;
	}
	fu_dell_dock_hub_add_instance(FU_DEVICE(hub), dock_type);
	fu_plugin_device_add(plugin, FU_DEVICE(hub));

	/* add hub instance id for the cached device */
	hub_dev = fu_plugin_cache_lookup(plugin, hub_cache_key);
	if (hub_dev != NULL) {
		fu_dell_dock_hub_add_instance(FU_DEVICE(hub_dev), dock_type);
		fu_plugin_device_add(plugin, FU_DEVICE(hub_dev));
		fu_plugin_cache_remove(plugin, hub_cache_key);
	}
	return TRUE;
}

static void
fu_dell_dock_plugin_separate_activation(FuPlugin *plugin)
{
	FuDevice *device_ec = fu_plugin_cache_lookup(plugin, "ec");
	FuDevice *device_usb4 = fu_plugin_cache_lookup(plugin, "usb4");

	/* both usb4 and ec device are found */
	if (device_usb4 && device_ec) {
		if (fu_device_has_flag(device_usb4, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION) &&
		    fu_device_has_flag(device_ec, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
			fu_device_remove_flag(FU_DEVICE(device_ec),
					      FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
			g_info("activate for %s is inhibited by %s",
			       fu_device_get_name(device_ec),
			       fu_device_get_name(device_usb4));
		}
	}
}

static void
fu_dell_dock_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	/* dell dock delays the activation so skips device restart */
	if (fu_device_has_guid(device, DELL_DOCK_TBT_INSTANCE_ID)) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SKIPS_RESTART);
		fu_plugin_cache_add(plugin, "tbt", device);
	}
	if (fu_device_has_guid(device, DELL_DOCK_USB4_INSTANCE_ID)) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SKIPS_RESTART);
		fu_plugin_cache_add(plugin, "usb4", device);
	}
	if (FU_IS_DELL_DOCK_EC(device))
		fu_plugin_cache_add(plugin, "ec", device);

	/* usb4 device from thunderbolt plugin */
	if (g_strcmp0(fu_device_get_plugin(device), "thunderbolt") == 0 &&
	    fu_device_has_guid(device, DELL_DOCK_USB4_INSTANCE_ID)) {
		g_autofree gchar *msg = NULL;
		msg = g_strdup_printf("firmware update inhibited by [%s] plugin",
				      fu_plugin_get_name(plugin));
		fu_device_inhibit(device, "hidden", msg);
		return;
	}

	/* online activation is mutually exclusive between usb4 and ec */
	fu_dell_dock_plugin_separate_activation(plugin);
}

static gboolean
fu_dell_dock_plugin_backend_device_removed(FuPlugin *plugin, FuDevice *device, GError **error)
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
fu_dell_dock_plugin_composite_prepare(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuDevice *parent = fu_dell_dock_plugin_get_ec(devices);
	const gchar *sku;
	if (parent == NULL)
		return TRUE;
	sku = fu_dell_dock_ec_get_module_type(parent);
	if (sku != NULL)
		fu_plugin_add_report_metadata(plugin, "DellDockSKU", sku);

	return TRUE;
}

static gboolean
fu_dell_dock_plugin_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuDevice *parent = fu_dell_dock_plugin_get_ec(devices);
	FuDevice *dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	gboolean needs_activation = FALSE;

	if (parent == NULL)
		return TRUE;

	/* if thunderbolt is in the transaction it needs to be activated separately */
	for (guint i = 0; i < devices->len; i++) {
		dev = g_ptr_array_index(devices, i);
		if ((g_strcmp0(fu_device_get_plugin(dev), "thunderbolt") == 0 ||
		     g_strcmp0(fu_device_get_plugin(dev), "intel_usb4") == 0 ||
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
	fu_dell_dock_plugin_separate_activation(plugin);

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

static void
fu_dell_dock_plugin_init(FuDellDockPlugin *self)
{
}

static void
fu_dell_dock_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
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

static void
fu_dell_dock_plugin_class_init(FuDellDockPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_dell_dock_plugin_constructed;
	plugin_class->device_registered = fu_dell_dock_plugin_device_registered;
	plugin_class->backend_device_added = fu_dell_dock_plugin_backend_device_added;
	plugin_class->backend_device_removed = fu_dell_dock_plugin_backend_device_removed;
	plugin_class->composite_cleanup = fu_dell_dock_plugin_composite_cleanup;
	plugin_class->composite_prepare = fu_dell_dock_plugin_composite_prepare;
}
