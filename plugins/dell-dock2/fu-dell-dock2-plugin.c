/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-dock2-common.h"
#include "fu-dell-dock2-plugin.h"

/* register the firmware types */
#include "fu-dell-dock2-dpmux-firmware.h"
#include "fu-dell-dock2-ec-firmware.h"
#include "fu-dell-dock2-pd-firmware.h"
#include "fu-dell-dock2-rtshub-firmware.h"

struct _FuDellDock2Plugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuDellDock2Plugin, fu_dell_dock2_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_dell_dock2_plugin_create_node(FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	fu_plugin_device_add(plugin, device);
	return TRUE;
}

static gboolean
fu_dell_dock2_plugin_device_add(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuDellDock2BaseType dock_type;
	FuDevice *ec_device = fu_plugin_cache_lookup(plugin, "ec");
	guint16 vid = fu_usb_device_get_vid(FU_USB_DEVICE(device));
	guint16 pid = fu_usb_device_get_pid(FU_USB_DEVICE(device));

	/* cache this device until dock type is seen */
	if (ec_device == NULL) {
		const gchar *key;
		key = g_strdup_printf("USB\\VID_%04X&PID_%04X", vid, pid);
		fu_plugin_cache_add(plugin, key, device);
		return TRUE;
	}

	/* dock type according to ec */
	dock_type = fu_dell_dock2_ec_get_dock_type(ec_device);
	if (dock_type == FU_DELL_DOCK2_BASE_TYPE_UNKNOWN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "can't read base dock type from EC");
		return FALSE;
	}

	/* RTS usb hub devices */
	if ((vid == DELL_VID && pid == DELL_DOCK2_USB_RTS5480_GEN1_PID) ||
	    (vid == DELL_VID && pid == DELL_DOCK2_USB_RTS5480_GEN2_PID) ||
	    (vid == DELL_VID && pid == DELL_DOCK2_USB_RTS5485_GEN2_PID)) {
		g_autoptr(FuDellDock2RtsHub) hub_device = NULL;
		g_autoptr(FuDeviceLocker) locker = NULL;

		hub_device = fu_dell_dock2_rtshub_new(FU_USB_DEVICE(device), dock_type);
		locker = fu_device_locker_new(FU_DEVICE(hub_device), error);
		if (locker == NULL)
			return FALSE;

		fu_device_add_child(ec_device, FU_DEVICE(hub_device));
	} else if (vid == DELL_DOCK2_RMM_USB_VID && pid == DELL_DOCK2_RMM_USB_PID) {
		g_autoptr(FuDellDock2Rmm) rmm_device = NULL;
		g_autoptr(FuDeviceLocker) locker = NULL;
		guint32 rmm_version_raw = fu_dell_dock2_ec_get_rmm_version(ec_device);

		rmm_device = fu_dell_dock2_rmm_new(FU_USB_DEVICE(device), dock_type);
		locker = fu_device_locker_new(FU_DEVICE(rmm_device), error);
		if (locker == NULL)
			return FALSE;

		fu_device_add_child(ec_device, FU_DEVICE(rmm_device));
		fu_dell_dock2_rmm_setup_version_raw(FU_DEVICE(rmm_device),
						    GUINT32_FROM_BE(rmm_version_raw));
	} else {
		g_debug("ignoring unknown device, vid: %04X, pid: %04X", vid, pid);
		return TRUE;
	}
	return TRUE;
}

static gboolean
fu_dell_dock2_plugin_ec_add_cached_devices(FuPlugin *plugin, FuDevice *ec_device, GError **error)
{
	struct {
		guint16 vid;
		guint16 pid;
	} hw_dev_ids[] = {
	    {DELL_VID, DELL_DOCK2_USB_RTS5480_GEN1_PID},
	    {DELL_VID, DELL_DOCK2_USB_RTS5480_GEN2_PID},
	    {DELL_VID, DELL_DOCK2_USB_RTS5485_GEN2_PID},
	    {DELL_DOCK2_RMM_USB_VID, DELL_DOCK2_RMM_USB_PID},
	    {0},
	};

	for (guint i = 0; hw_dev_ids[i].pid != 0; i++) {
		FuDevice *device;
		const gchar *key;

		key =
		    g_strdup_printf("USB\\VID_%04X&PID_%04X", hw_dev_ids[i].vid, hw_dev_ids[i].pid);

		device = fu_plugin_cache_lookup(plugin, key);
		if (device != NULL) {
			if (!(fu_dell_dock2_plugin_device_add(plugin, device, error)))
				return FALSE;

			fu_plugin_cache_remove(plugin, key);
		}
	}
	return TRUE;
}

static gboolean
fu_dell_dock2_plugin_backend_device_added(FuPlugin *plugin,
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
	if ((vid == DELL_VID && pid == DELL_DOCK2_HID_PID)) {
		g_autoptr(FuDellDock2Ec) ec_dev = NULL;
		g_autoptr(GError) error_local = NULL;

		ec_dev = fu_dell_dock2_ec_new(device);
		if (ec_dev == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "can't create EC V2 device");
			return FALSE;
		}

		if (fu_dell_dock2_plugin_create_node(plugin, FU_DEVICE(ec_dev), &error_local)) {
			/* flush the cached devices to plugin */
			if (!fu_dell_dock2_plugin_ec_add_cached_devices(plugin,
									FU_DEVICE(ec_dev),
									error))
				return FALSE;
		} else {
			/* api version 2 doesn't support legacy docks */
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
				g_debug("ignoring: %s", error_local->message);
			} else {
				g_propagate_error(error, g_steal_pointer(&error_local));
				return FALSE;
			}
		}
		return TRUE;
	}

	if (!fu_dell_dock2_plugin_device_add(plugin, device, error))
		return FALSE;

	return TRUE;
}

static void
fu_dell_dock2_plugin_config_mst_dev(FuDevice *device_mst)
{
	g_autofree const gchar *devname = NULL;
	guint16 vid = fu_usb_device_get_vid(FU_USB_DEVICE(device_mst));
	guint16 pid = fu_usb_device_get_pid(FU_USB_DEVICE(device_mst));

	/* vmm8430 */
	if (vid == MST_VMM8430_USB_VID && pid == MST_VMM8430_USB_PID)
		devname = g_strdup(
		    fu_dell_dock2_ec_devicetype_to_str(DELL_DOCK2_EC_DEV_TYPE_MST,
						       DELL_DOCK2_EC_DEV_MST_SUBTYPE_VMM8430,
						       0));

	/* vmm9430 */
	if (vid == MST_VMM9430_USB_VID && pid == MST_VMM9430_USB_PID)
		devname = g_strdup(
		    fu_dell_dock2_ec_devicetype_to_str(DELL_DOCK2_EC_DEV_TYPE_MST,
						       DELL_DOCK2_EC_DEV_MST_SUBTYPE_VMM9430,
						       0));

	/* device name */
	g_return_if_fail(devname != NULL);
	fu_device_set_name(device_mst, devname);

	/* flags */
	fu_device_add_internal_flag(device_mst, FU_DEVICE_INTERNAL_FLAG_EXPLICIT_ORDER);

	/* enforce install on k2 demo board */
	if (fu_device_has_internal_flag(device_mst, FU_DEVICE_INTERNAL_FLAG_ENFORCE_REQUIRES)) {
		fu_device_remove_internal_flag(device_mst,
					       FU_DEVICE_INTERNAL_FLAG_ENFORCE_REQUIRES);
	}
	return;
}

static void
fu_dell_dock2_plugin_setup_relationship(FuPlugin *plugin, FuDevice *device)
{
	FuDevice *device_ec = fu_plugin_cache_lookup(plugin, "ec");
	FuDevice *device_usb4 = fu_plugin_cache_lookup(plugin, "usb4");
	FuDevice *device_mst = fu_plugin_cache_lookup(plugin, "mst");

	if (device_ec && device_usb4 && !fu_device_get_parent(device_usb4)) {
		fu_device_add_child(device_ec, device_usb4);
		fu_plugin_cache_remove(plugin, "usb4");
	}

	if (device_ec && device_mst && !fu_device_get_parent(device_mst)) {
		fu_device_add_child(device_ec, device_mst);
		fu_plugin_cache_remove(plugin, "mst");
	}
}

static void
fu_dell_dock2_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	guint16 vid = 0;
	guint16 pid = 0;

	/* usb device of intreset */
	if (!FU_IS_USB_DEVICE(device))
		return;

	/* leverage intel_usb4 for usb4 devices */
	if (fu_device_has_guid(device, DELL_DOCK2_TBT4) ||
	    fu_device_has_guid(device, DELL_DOCK2_TBT5)) {
		/* default go through usb protocol instead thunderbolt */
		if (g_strcmp0(fu_device_get_plugin(device), "thunderbolt") == 0) {
			g_autofree gchar *msg = NULL;
			msg = g_strdup_printf("firmware update inhibited by [%s] plugin",
					      fu_plugin_get_name(plugin));
			fu_device_inhibit(device, "hidden", msg);
			return;
		}
		fu_device_add_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_EXPLICIT_ORDER);
		fu_plugin_cache_add(plugin, "usb4", device);
	}

	/* leverage synaptics_vmm9 plugin for the mst device */
	vid = fu_usb_device_get_vid(FU_USB_DEVICE(device));
	pid = fu_usb_device_get_pid(FU_USB_DEVICE(device));
	if ((vid == MST_VMM8430_USB_VID && pid == MST_VMM8430_USB_PID) ||
	    (vid == MST_VMM9430_USB_VID && pid == MST_VMM9430_USB_PID)) {
		fu_dell_dock2_plugin_config_mst_dev(device);
		fu_plugin_cache_add(plugin, "mst", device);
	}

	/* add ec to cache */
	if (FU_IS_DELL_DOCK2_EC(device))
		fu_plugin_cache_add(plugin, "ec", device);

	/* setup parent device */
	fu_dell_dock2_plugin_setup_relationship(plugin, device);
}

static gboolean
fu_dell_dock2_plugin_prepare(FuPlugin *plugin,
			     FuDevice *device,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* maybe device is the parent */
	if (parent == NULL)
		parent = device;

	/* ensure parent is dock ec */
	if (!FU_IS_DELL_DOCK2_EC(parent))
		return TRUE;

	/* open ec device */
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;

	/* check if dock is ready to process updates */
	if (!fu_dell_dock2_ec_is_dock_ready4update(parent, error))
		return FALSE;

	/* own the dock */
	if (!fu_dell_dock2_ec_modify_lock(parent, TRUE, error))
		return FALSE;

	/* usb4 device reboot is suppressed, let ec handle it in passive update */
	if (fu_device_has_guid(device, DELL_DOCK2_TBT4) ||
	    fu_device_has_guid(device, DELL_DOCK2_TBT5)) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SKIPS_RESTART);
	}

	g_debug("plugin prepared for (%s) successfully", fu_device_get_name(device));
	return TRUE;
}

static gboolean
fu_dell_dock2_plugin_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		FuDevice *parent = fu_device_get_parent(dev);
		g_autoptr(FuDeviceLocker) locker = NULL;

		/* maybe device is the parent */
		if (parent == NULL)
			parent = dev;

		/* lookup the dock ec */
		if (!FU_IS_DELL_DOCK2_EC(parent))
			continue;

		locker = fu_device_locker_new(parent, error);
		if (locker == NULL)
			return FALSE;

		/* always enable passive flow */
		if (!fu_dell_dock2_ec_run_passive_update(parent, error))
			return FALSE;

		/* release the dock */
		if (!fu_dell_dock2_ec_modify_lock(parent, FALSE, error))
			return FALSE;

		/* only run this once */
		g_debug("dock composite_cleanup is done with (%s) via (%s)",
			fu_device_get_name(dev),
			fu_device_get_name(parent));
		break;
	}
	return TRUE;
}

static void
fu_dell_dock2_plugin_init(FuDellDock2Plugin *self)
{
}

static void
fu_dell_dock2_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);

	/* allow these to be built by quirks */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_DOCK2_PACKAGE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_DOCK2_PD);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_DOCK2_DPMUX);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_DOCK2_WTPD);

	/* register firmware parser */
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_DELL_DOCK2_PD_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_DELL_DOCK2_EC_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_DELL_DOCK2_RTSHUB_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_DELL_DOCK2_DPMUX_FIRMWARE);
}

static void
fu_dell_dock2_plugin_class_init(FuDellDock2PluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_dell_dock2_plugin_constructed;
	plugin_class->device_registered = fu_dell_dock2_plugin_device_registered;
	plugin_class->backend_device_added = fu_dell_dock2_plugin_backend_device_added;
	plugin_class->composite_cleanup = fu_dell_dock2_plugin_composite_cleanup;
	plugin_class->prepare = fu_dell_dock2_plugin_prepare;
}
