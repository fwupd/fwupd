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

#include "fu-dell-dock-common.h"
#include "fu-dell-dock-i2c-pd-firmware.h"
#include "fu-dell-dock-plugin.h"

const DellDockComponent dock_component_hub[] = {
    {DOCK_BASE_TYPE_SALOMON, DELL_VID, DELL_DOCK_HID_PID, "USB\\VID_413C&PID_B06E&hub"},
    {DOCK_BASE_TYPE_SALOMON, DELL_VID, DELL_DOCK_USB_RTS5413_PID, "USB\\VID_413C&PID_B06F&hub"},
    {DOCK_BASE_TYPE_ATOMIC, DELL_VID, DELL_DOCK_HID_PID, "USB\\VID_413C&PID_B06E&atomic_hub"},
    {DOCK_BASE_TYPE_ATOMIC,
     DELL_VID,
     DELL_DOCK_USB_RTS5413_PID,
     "USB\\VID_413C&PID_B06F&atomic_hub"},
    {DOCK_BASE_TYPE_K2, DELL_VID, DELL_DOCK_USB_RTS5480_GEN1_PID, "USB\\VID_413C&PID_B0A1&k2_hub"},
    {DOCK_BASE_TYPE_K2, DELL_VID, DELL_DOCK_USB_RTS5480_GEN2_PID, "USB\\VID_413C&PID_B0A2&k2_hub"},
    {DOCK_BASE_TYPE_K2, DELL_VID, DELL_DOCK_USB_RTS5485_PID, "USB\\VID_413C&PID_B0A3&k2_hub"},
    {DOCK_BASE_TYPE_K2, DELL_VID, DELL_DOCK_USB_RMM_PID, "USB\\VID_413C&PID_B0A4&k2_rmm"},
    {DOCK_BASE_TYPE_UNKNOWN, 0, 0, NULL},
};

const DellDockComponent dock_component_mst[] = {
    {DOCK_BASE_TYPE_SALOMON, 0, 0, "MST-panamera-vmm5331-259"},
    {DOCK_BASE_TYPE_ATOMIC, 0, 0, "MST-cayenne-vmm6210-257"},
    {DOCK_BASE_TYPE_K2, MST_VMM8430_USB_VID, MST_VMM8430_USB_PID, "MST-carrera-vmm8430-261"},
    {DOCK_BASE_TYPE_K2, MST_VMM9430_USB_VID, MST_VMM9430_USB_PID, "MST-carrera-vmm9430-260"},
    {DOCK_BASE_TYPE_UNKNOWN, 0, 0, NULL},
};

const DellDockComponent dock_component_pkg[] = {
    {DOCK_BASE_TYPE_SALOMON, 0, 0, "USB\\VID_413C&PID_B06E&hub&status"},
    {DOCK_BASE_TYPE_SALOMON, 0, 1, "USB\\VID_413C&PID_B06E&hub&salomon_mlk_status"},
    {DOCK_BASE_TYPE_ATOMIC, 0, 0, "USB\\VID_413C&PID_B06E&hub&atomic_status"},
    {DOCK_BASE_TYPE_K2, 0, 1, "USB\\VID_413C&PID_B06E&hub&k2_sku1_pkg"},
    {DOCK_BASE_TYPE_K2, 0, 2, "USB\\VID_413C&PID_B06E&hub&k2_sku2_pkg"},
    {DOCK_BASE_TYPE_K2, 0, 3, "USB\\VID_413C&PID_B06E&hub&k2_sku3_pkg"},
    {DOCK_BASE_TYPE_UNKNOWN, 0, 0, NULL},
};

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

static DockBaseType
fu_dell_dock_plugin_get_dock_type(FuDevice *device, GError **error)
{
	if (FU_IS_DELL_DOCK_EC(device))
		return fu_dell_dock_get_dock_type(device);

	if (FU_IS_DELL_DOCK_EC_V2(device))
		return fu_dell_dock_ec_v2_get_dock_type(device);

	return DOCK_BASE_TYPE_UNKNOWN;
}

static gboolean
fu_dell_dock_plugin_probe_ec_v1_subcomponents(FuPlugin *plugin, FuDevice *ec_device, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	DockBaseType dock_type;

	/* determine dock type said by ec */
	dock_type = fu_dell_dock_plugin_get_dock_type(ec_device, error);
	if (dock_type == DOCK_BASE_TYPE_UNKNOWN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "can't read base dock type from EC");
		return FALSE;
	}

	/* MST */
	{
		g_autoptr(FuDellDockMst) mst_device = NULL;
		g_autofree const gchar *instance_id;
		g_autofree const gchar *instance_guid = NULL;

		mst_device = fu_dell_dock_mst_new(ctx);
		instance_id =
		    g_strdup(fu_dell_dock_get_instance_id(dock_type, dock_component_mst, 0, 0));
		instance_guid = fwupd_guid_hash_string(instance_id);
		fu_device_add_instance_id(FU_DEVICE(mst_device), instance_id);
		fu_device_add_guid(FU_DEVICE(mst_device), instance_guid);
		fu_device_add_child(FU_DEVICE(ec_device), FU_DEVICE(mst_device));

		if (!fu_device_probe(FU_DEVICE(mst_device), error))
			return FALSE;

		if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(mst_device), error))
			return FALSE;
	}

	/* PACKAGE */
	{
		g_autoptr(FuDellDockStatus) status_device = NULL;
		g_autofree const gchar *instance_id = NULL;
		g_autofree const gchar *instance_guid = NULL;
		guint16 dock_variant = 0;

		status_device = fu_dell_dock_status_new(ctx);
		dock_variant = fu_dell_dock_module_is_usb4(FU_DEVICE(ec_device)) ? 1 : 0;
		instance_id = g_strdup(
		    fu_dell_dock_get_instance_id(dock_type, dock_component_pkg, 0, dock_variant));
		instance_guid = fwupd_guid_hash_string(instance_id);
		fu_device_add_instance_id(FU_DEVICE(status_device), instance_id);
		fu_device_add_guid(FU_DEVICE(status_device), fwupd_guid_hash_string(instance_guid));
		fu_device_add_child(FU_DEVICE(ec_device), FU_DEVICE(status_device));
		if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(status_device), error))
			return FALSE;
	}

	/* TBT 3 */
	{
		g_autoptr(FuDellDockTbt) tbt_device = NULL;
		g_autofree const gchar *instance_id = NULL;
		g_autofree const gchar *instance_guid = NULL;

		if (fu_dell_dock_ec_needs_tbt(FU_DEVICE(ec_device))) {
			tbt_device = fu_dell_dock_tbt_new(fu_device_get_proxy(ec_device));
			instance_id = DELL_DOCK_TBT3;
			instance_guid = fwupd_guid_hash_string(instance_id);
			fu_device_add_guid(FU_DEVICE(tbt_device), instance_guid);
			fu_device_add_child(FU_DEVICE(ec_device), FU_DEVICE(tbt_device));
			if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(tbt_device), error))
				return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_dell_dock_plugin_probe_ec_v2_subcomponents(FuPlugin *plugin, FuDevice *ec_device, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	guint8 base_type = DOCK_BASE_TYPE_K2;

	/* PACKAGE */
	{
		g_autoptr(FuDellDockStatus) status_device = NULL;
		const gchar *instance_id = NULL;
		g_autofree const gchar *instance_guid = NULL;
		const gint dock_sku = fu_dell_dock_ec_v2_get_dock_sku(ec_device);

		status_device = fu_dell_dock_status_new(ctx);
		instance_id =
		    fu_dell_dock_get_instance_id(base_type, dock_component_pkg, 0, dock_sku);
		instance_guid = fwupd_guid_hash_string(fwupd_guid_hash_string(instance_id));
		fu_device_add_instance_id(FU_DEVICE(status_device), instance_id);
		fu_device_add_guid(FU_DEVICE(status_device), instance_guid);
		fu_device_add_child(ec_device, FU_DEVICE(status_device));
		if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(status_device), error))
			return FALSE;
	}

	/* PD UP5 */
	if (fu_dell_dock_ec_v2_dev_entry(ec_device,
					 EC_V2_DOCK_DEVICE_TYPE_PD,
					 EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI,
					 EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI_INSTANCE_UP5) != NULL) {
		g_autoptr(FuDellDockPd) pd_up5_device = NULL;

		pd_up5_device = fu_dell_dock_pd_new(ec_device,
						    EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI,
						    EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI_INSTANCE_UP5);
		if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(pd_up5_device), error))
			return FALSE;
	}

	/* PD UP15 */
	if (fu_dell_dock_ec_v2_dev_entry(ec_device,
					 EC_V2_DOCK_DEVICE_TYPE_PD,
					 EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI,
					 EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI_INSTANCE_UP15) != NULL) {
		g_autoptr(FuDellDockPd) pd_up15_device = NULL;

		pd_up15_device = fu_dell_dock_pd_new(ec_device,
						     EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI,
						     EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI_INSTANCE_UP15);
		if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(pd_up15_device), error))
			return FALSE;
	}

	/* PD UP17 */
	if (fu_dell_dock_ec_v2_dev_entry(ec_device,
					 EC_V2_DOCK_DEVICE_TYPE_PD,
					 EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI,
					 EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI_INSTANCE_UP17) != NULL) {
		g_autoptr(FuDellDockPd) pd_up17_device = NULL;

		pd_up17_device = fu_dell_dock_pd_new(ec_device,
						     EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI,
						     EC_V2_DOCK_DEVICE_PD_SUBTYPE_TI_INSTANCE_UP17);
		if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(pd_up17_device), error))
			return FALSE;
	}

	/* DP MUX */
	if (fu_dell_dock_ec_v2_dev_entry(ec_device, EC_V2_DOCK_DEVICE_TYPE_DP_MUX, 0, 0) != NULL) {
		g_autoptr(FuDellDockDpmux) dpmux_device = NULL;

		dpmux_device = fu_dell_dock_dpmux_new(ec_device);
		if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(dpmux_device), error))
			return FALSE;
	}

	/* WELTREND PD */
	if (fu_dell_dock_ec_v2_dev_entry(ec_device, EC_V2_DOCK_DEVICE_TYPE_WTPD, 0, 0) != NULL) {
		g_autoptr(FuDellDockWtpd) weltrend_device = NULL;

		weltrend_device = fu_dell_dock_wtpd_new(ec_device);
		if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(weltrend_device), error))
			return FALSE;
	}

	return TRUE;
}

static FuDevice *
fu_dell_dock_plugin_get_ec_device(FuPlugin *plugin)
{
	GPtrArray *devices = fu_plugin_get_devices(plugin);

	for (gint i = devices->len - 1; i >= 0; i--) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		if (FU_IS_DELL_DOCK_EC_V2(dev))
			return dev;
		if (FU_IS_DELL_DOCK_EC(dev))
			return dev;
	}
	return NULL;
}

static gboolean
fu_dell_dock_plugin_device_add(FuPlugin *plugin,
			       FuDevice *device,
			       gboolean is_newdev,
			       GError **error)
{
	FuDevice *ec_device;
	DockBaseType dock_type;
	guint16 vid, pid;
	g_autofree const gchar *instance_id = NULL;

	/* VID and PID */
	vid = fu_usb_device_get_vid(FU_USB_DEVICE(device));
	pid = fu_usb_device_get_pid(FU_USB_DEVICE(device));

	/* cache current device until EC dock type is available */
	ec_device = fu_dell_dock_plugin_get_ec_device(plugin);
	if (ec_device == NULL) {
		const gchar *key;
		key = g_strdup_printf("USB\\VID_%04X&PID_%04X", vid, pid);
		fu_plugin_cache_add(plugin, key, device);
		return TRUE;
	}

	/* determine dock type said by ec */
	dock_type = fu_dell_dock_plugin_get_dock_type(ec_device, error);
	if (dock_type == DOCK_BASE_TYPE_UNKNOWN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "can't read base dock type from EC");
		return FALSE;
	}

	/* USB HUB Devices */
	instance_id =
	    g_strdup(fu_dell_dock_get_instance_id(dock_type, dock_component_hub, vid, pid));
	if (instance_id != NULL) {
		g_autofree const gchar *instance_guid = NULL;
		g_autoptr(FuDellDockHub) hub_device = NULL;
		g_autoptr(FuDeviceLocker) locker = NULL;

		instance_guid = fwupd_guid_hash_string(instance_id);
		if (is_newdev) {
			hub_device = fu_dell_dock_hub_new(FU_USB_DEVICE(device));
			locker = fu_device_locker_new(FU_DEVICE(hub_device), error);
			if (locker == NULL)
				return FALSE;

			fu_device_add_instance_id(FU_DEVICE(hub_device), instance_id);
			fu_device_add_guid(FU_DEVICE(hub_device), instance_guid);
			fu_device_add_child(ec_device, FU_DEVICE(hub_device));
			fu_plugin_device_add(plugin, FU_DEVICE(hub_device));
		} else {
			fu_device_add_instance_id(device, instance_id);
			fu_device_add_guid(device, instance_guid);
			fu_device_add_child(ec_device, device);
			fu_plugin_device_add(plugin, device);
		}
	}

	return TRUE;
}

static gboolean
fu_dell_dock_plugin_ec_add_cached_devices(FuPlugin *plugin, FuDevice *ec_device, GError **error)
{
	DockBaseType dock_type;
	g_autofree const gchar *instance_id = NULL;

	/* determine dock type said by ec */
	dock_type = fu_dell_dock_plugin_get_dock_type(ec_device, error);
	if (dock_type == DOCK_BASE_TYPE_UNKNOWN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "can't read base dock type from EC");
		return FALSE;
	}

	for (guint i = 0; dock_component_hub[i].instance_id != NULL; i++) {
		FuDevice *device;
		guint16 vid, pid;
		const gchar *key;

		if (dock_component_hub[i].dock_type != dock_type)
			continue;

		vid = dock_component_hub[i].vid;
		pid = dock_component_hub[i].pid;
		key = g_strdup_printf("USB\\VID_%04X&PID_%04X", vid, pid);

		device = fu_plugin_cache_lookup(plugin, key);
		if (device != NULL) {
			if (!(fu_dell_dock_plugin_device_add(plugin, device, TRUE, error)))
				return FALSE;

			fu_plugin_cache_remove(plugin, key);
		}
	}
	return TRUE;
}

static gboolean
fu_dell_dock_plugin_backend_device_added(FuPlugin *plugin,
					 FuDevice *device,
					 FuProgress *progress,
					 GError **error)
{
	g_autofree const gchar *instance_id = NULL;
	guint16 vid, pid;

	/* not interesting */
	if (!FU_IS_USB_DEVICE(device))
		return TRUE;

	/* VID and PID */
	vid = fu_usb_device_get_vid(FU_USB_DEVICE(device));
	pid = fu_usb_device_get_pid(FU_USB_DEVICE(device));

	/* USB HUB HID bridge device */
	if ((vid == DELL_VID && pid == DELL_DOCK_HID_PID)) {
		gboolean ec_done = FALSE;

		/* API version 2 */
		{
			g_autoptr(FuDellDockEcV2) ec_v2_dev = NULL;
			g_autoptr(GError) error_local = NULL;

			ec_v2_dev = fu_dell_dock_ec_v2_new(device);
			if (ec_v2_dev == NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "can't create EC V2 device");
				return FALSE;
			}

			if (fu_dell_dock_plugin_create_node(plugin,
							    FU_DEVICE(ec_v2_dev),
							    &error_local)) {
				if (!fu_dell_dock_plugin_probe_ec_v2_subcomponents(
					plugin,
					FU_DEVICE(ec_v2_dev),
					error))
					return FALSE;

				/* flush the cached devices to plugin */
				if (!fu_dell_dock_plugin_ec_add_cached_devices(plugin,
									       FU_DEVICE(ec_v2_dev),
									       error))
					return FALSE;

				ec_done = TRUE;
			} else {
				/* it is acceptable if API v2 is unaffordable */
				if (g_error_matches(error_local,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_FOUND)) {
					g_debug("ignoring: %s", error_local->message);
				} else {
					g_propagate_error(error, g_steal_pointer(&error_local));
					return FALSE;
				}
			}
		}

		/* API version 1 */
		if (!ec_done) {
			g_autoptr(FuDellDockHub) hub_device = NULL;
			g_autoptr(FuDellDockEc) ec_v1_dev = NULL;
			g_autoptr(FuDeviceLocker) locker = NULL;

			hub_device = fu_dell_dock_hub_new(FU_USB_DEVICE(device));
			locker = fu_device_locker_new(FU_DEVICE(hub_device), error);
			if (locker == NULL)
				return FALSE;

			/* create ec device */
			ec_v1_dev = fu_dell_dock_ec_new(FU_DEVICE(hub_device));
			if (ec_v1_dev == NULL)
				return FALSE;

			if (!fu_dell_dock_plugin_create_node(plugin, FU_DEVICE(ec_v1_dev), error))
				return FALSE;

			/* add dock ec sub-components */
			if (!fu_dell_dock_plugin_probe_ec_v1_subcomponents(plugin,
									   FU_DEVICE(ec_v1_dev),
									   error))
				return FALSE;

			/* add the hub device */
			if (!(fu_dell_dock_plugin_device_add(plugin,
							     FU_DEVICE(hub_device),
							     FALSE,
							     error)))
				return FALSE;

			/* flush the cached devices to plugin */
			if (!fu_dell_dock_plugin_ec_add_cached_devices(plugin,
								       FU_DEVICE(ec_v1_dev),
								       error))
				return FALSE;
		}
		return TRUE;
	}

	if (!fu_dell_dock_plugin_device_add(plugin, device, TRUE, error))
		return FALSE;

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
fu_dell_dock_plugin_config_mst_dev(FuDevice *device_ec, FuDevice *device_mst)
{
	g_autofree const gchar *instance_id = NULL;
	g_autofree const gchar *devname = NULL;
	guint16 vid = fu_usb_device_get_vid(FU_USB_DEVICE(device_mst));
	guint16 pid = fu_usb_device_get_pid(FU_USB_DEVICE(device_mst));
	DockBaseType dock_type = fu_dell_dock_plugin_get_dock_type(device_ec, NULL);

	/* set device name */
	if (vid == MST_VMM8430_USB_VID && pid == MST_VMM8430_USB_PID)
		devname = g_strdup(
		    fu_dell_dock_ec_v2_devicetype_to_str(EC_V2_DOCK_DEVICE_TYPE_MST,
							 EC_V2_DOCK_DEVICE_MST_SUBTYPE_VMM8430,
							 0));

	if (vid == MST_VMM9430_USB_VID && pid == MST_VMM9430_USB_PID)
		devname = g_strdup(
		    fu_dell_dock_ec_v2_devicetype_to_str(EC_V2_DOCK_DEVICE_TYPE_MST,
							 EC_V2_DOCK_DEVICE_MST_SUBTYPE_VMM9430,
							 0));

	g_return_if_fail(devname != NULL);
	fu_device_set_name(device_mst, devname);

	/* set device instance id */
	instance_id =
	    g_strdup(fu_dell_dock_get_instance_id(dock_type, dock_component_mst, vid, pid));
	fu_device_add_instance_id(device_mst, instance_id);

	return;
}

static void
fu_dell_dock_plugin_setup_relationship(FuPlugin *plugin, FuDevice *device)
{
	FuDevice *device_ec = fu_plugin_cache_lookup(plugin, "ec");
	FuDevice *device_tbt = fu_plugin_cache_lookup(plugin, "tbt");
	FuDevice *device_usb4 = fu_plugin_cache_lookup(plugin, "usb4");
	FuDevice *device_mst = fu_plugin_cache_lookup(plugin, "mst");

	if (device_ec && device_tbt && !fu_device_get_parent(device_tbt)) {
		fu_device_add_child(device_ec, device_tbt);
		fu_plugin_cache_remove(plugin, "tbt");
	}

	if (device_ec && device_usb4 && !fu_device_get_parent(device_usb4)) {
		fu_device_add_child(device_ec, device_usb4);
		fu_plugin_cache_remove(plugin, "usb4");
	}

	if (device_ec && device_mst && !fu_device_get_parent(device_mst)) {
		fu_dell_dock_plugin_config_mst_dev(device_ec, device_mst);
		fu_device_add_child(device_ec, device_mst);
		fu_plugin_cache_remove(plugin, "mst");
	}
}

static void
fu_dell_dock_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	/* dell dock delays the activation so skips device restart */
	if (fu_device_has_guid(device, DELL_DOCK_TBT3)) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SKIPS_RESTART);
		fu_plugin_cache_add(plugin, "tbt", device);
	}
	if (fu_device_has_guid(device, DELL_DOCK_TBT4) ||
	    fu_device_has_guid(device, DELL_DOCK_TBT5) ||
	    fu_device_has_guid(device, DELL_DOCK_TBT4_K2)) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SKIPS_RESTART);
		fu_plugin_cache_add(plugin, "usb4", device);
	}

	/* leverage synaptics_vmm9 plugin for mst device */
	if (FU_IS_USB_DEVICE(device)) {
		guint16 vid = fu_usb_device_get_vid(FU_USB_DEVICE(device));
		guint16 pid = fu_usb_device_get_pid(FU_USB_DEVICE(device));

		if ((vid == MST_VMM8430_USB_VID && pid == MST_VMM8430_USB_PID) ||
		    (vid == MST_VMM9430_USB_VID && pid == MST_VMM9430_USB_PID)) {
			fu_plugin_cache_add(plugin, "mst", device);
		}
	}

	/* add ec to cache */
	if (FU_IS_DELL_DOCK_EC(device) || FU_IS_DELL_DOCK_EC_V2(device))
		fu_plugin_cache_add(plugin, "ec", device);

	/* usb4 device from thunderbolt plugin */
	if (g_strcmp0(fu_device_get_plugin(device), "thunderbolt") == 0 &&
	    fu_device_has_guid(device, DELL_DOCK_TBT4)) {
		g_autofree gchar *msg = NULL;
		msg = g_strdup_printf("firmware update inhibited by [%s] plugin",
				      fu_plugin_get_name(plugin));
		fu_device_inhibit(device, "hidden", msg);
		return;
	}

	/* online activation is mutually exclusive between usb4 and ec */
	fu_dell_dock_plugin_separate_activation(plugin);

	/* setup parent device */
	fu_dell_dock_plugin_setup_relationship(plugin, device);
}

static gboolean
fu_dell_dock_plugin_backend_device_removed(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuDevice *parent;

	/* find the parent and ask daemon to remove whole chain  */
	parent = fu_device_get_parent(device);
	if (parent != NULL) {
		g_debug("Removing %s (%s)", fu_device_get_name(parent), fu_device_get_id(parent));
		fu_plugin_device_remove(plugin, parent);
	}

	return TRUE;
}

static gboolean
fu_dell_dock_plugin_composite_prepare(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	g_autofree GPtrArray *ec_devices = g_ptr_array_new();

	/* if we can support multiple docks simultaneously */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		FuDevice *ec_dev;

		ec_dev = fu_device_get_parent(device);
		if (!ec_dev) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "no valid EC device found for %s",
				    fu_device_get_name(device));
			continue;
		}

		// Add ec_dev to ec_devices
		if (!g_ptr_array_find(ec_devices, ec_dev, NULL)) {
			g_ptr_array_add(ec_devices, ec_dev);
		}
	}

	for (guint i = 0; i < ec_devices->len; i++) {
		FuDevice *ec_dev = g_ptr_array_index(ec_devices, i);
		const gchar *dock_type = NULL;

		if (FU_IS_DELL_DOCK_EC(ec_dev))
			dock_type = fu_dell_dock_ec_get_module_type(ec_dev);

		if (FU_IS_DELL_DOCK_EC_V2(ec_dev))
			dock_type = fu_dell_dock_ec_v2_get_data_module_type(ec_dev);

		if (dock_type != NULL) {
			g_autofree const gchar *key = g_strdup_printf("DellDockType%u", i);
			fu_plugin_add_report_metadata(plugin, key, dock_type);
		}
	}

	return TRUE;
}

static gboolean
fu_dell_dock_plugin_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	gboolean is_passive_flow_triggered = FALSE;

	/* if thunderbolt is in the transaction it needs to be activated separately */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		FuDevice *ec_dev;
		gboolean immediate_activation = FALSE;

		ec_dev = fu_device_get_parent(dev);
		if (ec_dev == NULL)
			return TRUE;

		/* tbt devices */
		if ((g_strcmp0(fu_device_get_plugin(dev), "thunderbolt") == 0 ||
		     g_strcmp0(fu_device_get_plugin(dev), "intel_usb4") == 0) &&
		    fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
			/* run the update immediately if necessary */
			if (!fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE)) {
				immediate_activation = TRUE;
				break;
			}

			/* let EC device activate the tbt device */
			if (FU_IS_DELL_DOCK_EC(ec_dev))
				if (!fu_dell_dock_ec_enable_tbt_passive(ec_dev))
					immediate_activation = TRUE;

			if (FU_IS_DELL_DOCK_EC_V2(ec_dev))
				if (!fu_dell_dock_ec_v2_enable_tbt_passive(ec_dev))
					immediate_activation = TRUE;

			/* separate activation flag between usb4 and ec device */
			fu_dell_dock_plugin_separate_activation(plugin);
		}

		if (!is_passive_flow_triggered) {
			g_autoptr(FuDeviceLocker) locker = NULL;
			locker = fu_device_locker_new(ec_dev, error);
			if (locker == NULL)
				return FALSE;

			if (FU_IS_DELL_DOCK_EC(ec_dev)) {
				if (!fu_dell_dock_ec_trigger_passive_flow(ec_dev, error))
					return FALSE;
			}

			if (FU_IS_DELL_DOCK_EC_V2(ec_dev)) {
				if (!fu_dell_dock_ec_v2_trigger_passive_flow(ec_dev, error))
					return FALSE;
			}

			if (!fu_device_locker_close(locker, error))
				return FALSE;

			is_passive_flow_triggered = TRUE;
		}

		/* activate, authenticate or commit the update immediately */
		if (immediate_activation &&
		    fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)) {
			g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
			if (!fu_device_activate(dev, progress, error))
				return FALSE;
		}
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
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_DOCK_PD);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_DOCK_DPMUX);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_DOCK_MST);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_DELL_DOCK_PD_FIRMWARE);
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
