/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-kestrel-common.h"
#include "fu-dell-kestrel-plugin.h"

/* register the firmware types */
#include "fu-dell-kestrel-rtshub-firmware.h"

/* plugin config */
#define FWUPD_DELL_KESTREL_PLUGIN_CONFIG_UOD "UpdateOnDisconnect"

struct _FuDellKestrelPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuDellKestrelPlugin, fu_dell_kestrel_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_dell_kestrel_plugin_create_node(FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	fu_plugin_device_add(plugin, device);
	return TRUE;
}

static gboolean
fu_dell_kestrel_plugin_device_add(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuDellDockBaseType dock_type;
	FuDevice *ec_device = fu_plugin_cache_lookup(plugin, "ec");
	guint16 vid = fu_device_get_vid(device);
	guint16 pid = fu_device_get_pid(device);

	/* cache this device until dock type is seen */
	if (ec_device == NULL) {
		g_autofree gchar *key = g_strdup_printf("USB\\VID_%04X&PID_%04X", vid, pid);
		fu_plugin_cache_add(plugin, key, device);
		return TRUE;
	}

	/* dock type according to ec */
	dock_type = fu_dell_kestrel_ec_get_dock_type(FU_DELL_KESTREL_EC(ec_device));
	if (dock_type == FU_DELL_DOCK_BASE_TYPE_UNKNOWN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "can't read base dock type from EC");
		return FALSE;
	}

	/* dell devices */
	if (vid != DELL_VID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device vid not dell, got: 0x%04x",
			    vid);
		return FALSE;
	}

	/* Remote Management */
	if (pid == DELL_KESTREL_USB_RMM_PID) {
		g_autoptr(FuDellKestrelRmm) rmm_device = NULL;
		g_autoptr(FuDeviceLocker) locker = NULL;

		rmm_device = fu_dell_kestrel_rmm_new(FU_USB_DEVICE(device));
		if (rmm_device == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "failed to create rmm device");
			return FALSE;
		}

		locker = fu_device_locker_new(FU_DEVICE(rmm_device), error);
		if (locker == NULL)
			return FALSE;

		fu_device_add_child(ec_device, FU_DEVICE(rmm_device));
		fu_dell_kestrel_rmm_fix_version(rmm_device);

		return TRUE;
	}

	/* RTS usb hub devices */
	if (pid == DELL_KESTREL_USB_RTS0_G1_PID || pid == DELL_KESTREL_USB_RTS0_G2_PID ||
	    pid == DELL_KESTREL_USB_RTS5_G2_PID) {
		g_autoptr(FuDellKestrelRtsHub) hub_device = NULL;
		g_autoptr(FuDeviceLocker) locker = NULL;

		hub_device = fu_dell_kestrel_rtshub_new(FU_USB_DEVICE(device), dock_type);
		if (hub_device == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to create rtshub device, pid: 0x%04x",
				    pid);
			return FALSE;
		}

		locker = fu_device_locker_new(FU_DEVICE(hub_device), error);
		if (locker == NULL)
			return FALSE;

		fu_device_add_child(ec_device, FU_DEVICE(hub_device));
		return TRUE;
	}

	/* devices added from quirk only the RTSHUB */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "ignoring unsupported device, vid: 0x%04x, pid: 0x%04x",
		    vid,
		    pid);
	return FALSE;
}

static gboolean
fu_dell_kestrel_plugin_ec_add_cached_devices(FuPlugin *plugin, FuDevice *ec_device, GError **error)
{
	struct {
		guint16 vid;
		guint16 pid;
	} hw_dev_ids[] = {
	    {DELL_VID, DELL_KESTREL_USB_RTS0_G1_PID},
	    {DELL_VID, DELL_KESTREL_USB_RTS0_G2_PID},
	    {DELL_VID, DELL_KESTREL_USB_RTS5_G2_PID},
	    {DELL_VID, DELL_KESTREL_USB_RMM_PID},
	    {0},
	};

	for (guint i = 0; hw_dev_ids[i].pid != 0; i++) {
		FuDevice *device;
		g_autofree gchar *key = NULL;

		key =
		    g_strdup_printf("USB\\VID_%04X&PID_%04X", hw_dev_ids[i].vid, hw_dev_ids[i].pid);
		device = fu_plugin_cache_lookup(plugin, key);
		if (device != NULL) {
			if (!(fu_dell_kestrel_plugin_device_add(plugin, device, error)))
				return FALSE;

			fu_plugin_cache_remove(plugin, key);
		}
	}
	return TRUE;
}

static gboolean
fu_dell_kestrel_plugin_backend_device_added(FuPlugin *plugin,
					    FuDevice *device,
					    FuProgress *progress,
					    GError **error)
{
	guint16 vid, pid;

	/* not interesting */
	if (!FU_IS_USB_DEVICE(device))
		return TRUE;

	/* VID and PID */
	vid = fu_device_get_vid(device);
	pid = fu_device_get_pid(device);

	/* USB HUB HID bridge device */
	if ((vid == DELL_VID && pid == DELL_KESTREL_HID_PID)) {
		gboolean uod;
		g_autoptr(FuDellKestrelEc) ec_dev = NULL;
		g_autoptr(GError) error_local = NULL;

		uod = fu_plugin_get_config_value_boolean(plugin,
							 FWUPD_DELL_KESTREL_PLUGIN_CONFIG_UOD);
		ec_dev = fu_dell_kestrel_ec_new(device, uod);
		if (ec_dev == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "can't create EC V2 device");
			return FALSE;
		}

		if (fu_dell_kestrel_plugin_create_node(plugin, FU_DEVICE(ec_dev), &error_local)) {
			/* flush the cached devices to plugin */
			if (!fu_dell_kestrel_plugin_ec_add_cached_devices(plugin,
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

	if (!fu_dell_kestrel_plugin_device_add(plugin, device, error))
		return FALSE;

	return TRUE;
}

static void
fu_dell_kestrel_plugin_config_mst_dev(FuPlugin *plugin)
{
	FuDevice *device_ec = fu_plugin_cache_lookup(plugin, "ec");
	FuDevice *device_mst = fu_plugin_cache_lookup(plugin, "mst");
	FuDellKestrelEcDevType mst_devtype = FU_DELL_KESTREL_EC_DEV_TYPE_MST;
	FuDellKestrelEcDevSubtype mst_subtype;
	const gchar *devname = NULL;

	if (device_ec == NULL || device_mst == NULL)
		return;

	/* run only once */
	if (fu_device_has_private_flag(device_mst, FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER))
		return;

	/* vmm8 */
	mst_subtype = FU_DELL_KESTREL_EC_DEV_SUBTYPE_VMM8;
	if (fu_dell_kestrel_ec_is_dev_present(FU_DELL_KESTREL_EC(device_ec),
					      mst_devtype,
					      mst_subtype,
					      0))
		devname = fu_dell_kestrel_ec_devicetype_to_str(mst_devtype, mst_subtype, 0);

	/* vmm9 */
	mst_subtype = FU_DELL_KESTREL_EC_DEV_SUBTYPE_VMM9;
	if (fu_dell_kestrel_ec_is_dev_present(FU_DELL_KESTREL_EC(device_ec),
					      mst_devtype,
					      mst_subtype,
					      0))
		devname = fu_dell_kestrel_ec_devicetype_to_str(mst_devtype, mst_subtype, 0);

	/* device name */
	if (devname == NULL) {
		g_warning("no mst device found in ec, device name is undetermined");
		return;
	}
	fu_device_set_name(device_mst, devname);

	/* flags */
	fu_device_add_private_flag(device_mst, FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER);
	fu_device_add_private_flag(device_mst, FU_DEVICE_PRIVATE_FLAG_SKIPS_RESTART);
	return;
}

static void
fu_dell_kestrel_plugin_config_parentship(FuPlugin *plugin)
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
fu_dell_kestrel_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	/* leverage intel_usb4 for usb4 devices */
	if (fu_device_has_guid(device, DELL_KESTREL_T4_DEVID) ||
	    fu_device_has_guid(device, DELL_KESTREL_T5_DEVID)) {
		/* default go through usb protocol instead thunderbolt */
		if (g_strcmp0(fu_device_get_plugin(device), "thunderbolt") == 0) {
			g_autofree gchar *msg =
			    g_strdup_printf("firmware update inhibited by [%s] plugin",
					    fu_plugin_get_name(plugin));
			fu_device_inhibit(device, "hidden", msg);
			return;
		}
		/* activation should already done when device is added */
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
		fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER);
		fu_plugin_cache_add(plugin, "usb4", device);
	}

	/* usb device of interset */
	if (!FU_IS_USB_DEVICE(device))
		return;

	/* leverage synaptics_vmm9 plugin for the mst device */
	if (fu_device_get_vid(device) == MST_VMM89_USB_VID &&
	    fu_device_get_pid(device) == MST_VMM89_USB_PID)
		fu_plugin_cache_add(plugin, "mst", device);

	/* add ec to cache */
	if (FU_IS_DELL_KESTREL_EC(device))
		fu_plugin_cache_add(plugin, "ec", device);

	/* config mst device */
	fu_dell_kestrel_plugin_config_mst_dev(plugin);

	/* setup parent device */
	fu_dell_kestrel_plugin_config_parentship(plugin);
}

static FuDevice *
fu_dell_kestrel_plugin_get_ec_from_devices(GPtrArray *devices)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);
		FuDevice *parent = fu_device_get_parent(dev);

		if (parent == NULL)
			parent = dev;

		if (FU_IS_DELL_KESTREL_EC(parent))
			return parent;
	}
	return NULL;
}

static gboolean
fu_dell_kestrel_plugin_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuDevice *ec_dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* locate the ec device */
	ec_dev = fu_dell_kestrel_plugin_get_ec_from_devices(devices);
	if (ec_dev == NULL)
		return TRUE;

	/* open ec device */
	locker = fu_device_locker_new(ec_dev, error);
	if (locker == NULL)
		return FALSE;

	/* release the dock */
	if (!fu_dell_kestrel_ec_own_dock(FU_DELL_KESTREL_EC(ec_dev), FALSE, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_dell_kestrel_plugin_composite_prepare(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuDevice *ec_dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* locate the ec device */
	ec_dev = fu_dell_kestrel_plugin_get_ec_from_devices(devices);
	if (ec_dev == NULL)
		return TRUE;

	/* open ec device */
	locker = fu_device_locker_new(ec_dev, error);
	if (locker == NULL)
		return FALSE;

	/* check if dock is ready to process updates */
	if (!fu_dell_kestrel_ec_is_dock_ready4update(ec_dev, error))
		return FALSE;

	/* own the dock */
	if (!fu_dell_kestrel_ec_own_dock(FU_DELL_KESTREL_EC(ec_dev), TRUE, error))
		return FALSE;

	/* conditionally enable passive flow */
	if (fu_plugin_get_config_value_boolean(plugin, FWUPD_DELL_KESTREL_PLUGIN_CONFIG_UOD)) {
		if (fu_device_has_flag(ec_dev, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE)) {
			if (!fu_dell_kestrel_ec_run_passive_update(FU_DELL_KESTREL_EC(ec_dev),
								   error))
				return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_dell_kestrel_plugin_modify_config(FuPlugin *plugin,
				     const gchar *key,
				     const gchar *value,
				     GError **error)
{
	const gchar *keys[] = {FWUPD_DELL_KESTREL_PLUGIN_CONFIG_UOD, NULL};
	if (!g_strv_contains(keys, key)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "config key %s not supported",
			    key);
		return FALSE;
	}
	return fu_plugin_set_config_value(plugin, key, value, error);
}

static gboolean
fu_dell_kestrel_plugin_backend_device_removed(FuPlugin *plugin, FuDevice *device, GError **error)
{
	const gchar *cache_keys[] = {"ec", "mst", "usb4"};
	FuDevice *parent = fu_device_get_parent(device);

	if (parent == NULL)
		return TRUE;
	if (!FU_IS_DELL_KESTREL_EC(parent))
		return TRUE;

	if (FU_IS_USB_DEVICE(device)) {
		g_autofree gchar *key = g_strdup_printf("USB\\VID_%04X&PID_%04X",
							fu_device_get_vid(device),
							fu_device_get_pid(device));
		fu_plugin_cache_remove(plugin, key);
	}
	for (gsize i = 0; i < G_N_ELEMENTS(cache_keys); i++)
		fu_plugin_cache_remove(plugin, cache_keys[i]);

	return TRUE;
}

static gboolean
fu_dell_kestrel_plugin_prepare(FuPlugin *plugin,
			       FuDevice *device,
			       FuProgress *progress,
			       FwupdInstallFlags flags,
			       GError **error)
{
	/* usb4 device reboot is suppressed, let ec handle it in passive update */
	if (fu_device_has_guid(device, DELL_KESTREL_T4_DEVID) ||
	    fu_device_has_guid(device, DELL_KESTREL_T5_DEVID)) {
		/* uod requires needs-activate from intel-usb4 plugin */
		if (fu_plugin_get_config_value_boolean(plugin,
						       FWUPD_DELL_KESTREL_PLUGIN_CONFIG_UOD))
			fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_SKIPS_RESTART);
	}

	return TRUE;
}

static void
fu_dell_kestrel_plugin_init(FuDellKestrelPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_dell_kestrel_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);

	/* allow these to be built by quirks */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_KESTREL_PACKAGE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_KESTREL_PD);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_KESTREL_DPMUX);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_KESTREL_WTPD);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_KESTREL_ILAN);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_KESTREL_RMM);	 /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_KESTREL_EC);	 /* coverage */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_KESTREL_RTSHUB); /* coverage */

	/* register firmware parser */
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_DELL_KESTREL_RTSHUB_FIRMWARE);

	/* defaults changed here will also be reflected in the fwupd.conf man page */
	fu_plugin_set_config_default(plugin, FWUPD_DELL_KESTREL_PLUGIN_CONFIG_UOD, "true");
}

static void
fu_dell_kestrel_plugin_class_init(FuDellKestrelPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_dell_kestrel_plugin_constructed;
	plugin_class->device_registered = fu_dell_kestrel_plugin_device_registered;
	plugin_class->backend_device_added = fu_dell_kestrel_plugin_backend_device_added;
	plugin_class->backend_device_removed = fu_dell_kestrel_plugin_backend_device_removed;
	plugin_class->composite_prepare = fu_dell_kestrel_plugin_composite_prepare;
	plugin_class->composite_cleanup = fu_dell_kestrel_plugin_composite_cleanup;
	plugin_class->modify_config = fu_dell_kestrel_plugin_modify_config;
	plugin_class->prepare = fu_dell_kestrel_plugin_prepare;
}
