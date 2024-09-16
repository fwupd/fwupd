
/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libmm-glib.h>
#include <string.h>

#include "fu-mm-device.h"

/* amount of time to wait for ports of the same device	being exposed by kernel */
#define FU_MM_UDEV_DEVICE_PORTS_TIMEOUT 3 /* s */

/* out-of-tree modem-power driver is unsupported */
#define MODEM_POWER_SYSFS_PATH "/sys/class/modem-power"

struct FuPluginData {
	MMManager *manager;
	gboolean manager_ready;
	GFileMonitor *modem_power_monitor;
	guint udev_timeout_id;

	/* when a device is inhibited from MM, we store all relevant details
	 * ourselves to recreate a functional device object even without MM
	 */
	FuMmDevice *shadow_device;

	/*
	 * Used to mark whether FU_MM_DEVICE_FLAG_UNINHIBIT_MM_AFTER_FASTBOOT_REBOOT is being used
	 */
	gboolean device_ready_uninhibit_manager;
};

typedef FuPluginData FuModemManagerPlugin;
#define FU_MODEM_MANAGER_PLUGIN(o) fu_plugin_get_data(FU_PLUGIN(o))

static void
fu_mm_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);
	fwupd_codec_string_append_bool(str, idt, "ManagerReady", self->manager_ready);
	if (self->shadow_device != NULL) {
		fwupd_codec_string_append(str,
					  idt,
					  "ShadowDevice",
					  fu_device_get_id(self->shadow_device));
	}
}

static void
fu_mm_plugin_load(FuContext *ctx)
{
	fu_context_add_quirk_key(ctx, "ModemManagerBranchAtCommand");
}

static void
fu_mm_plugin_udev_device_removed(FuPlugin *plugin)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);
	FuMmDevice *dev;

	if (self->shadow_device == NULL)
		return;

	dev = fu_plugin_cache_lookup(plugin,
				     fu_device_get_physical_id(FU_DEVICE(self->shadow_device)));
	if (dev == NULL)
		return;

	/* once the first port is gone, consider device is gone */
	fu_plugin_cache_remove(plugin, fu_device_get_physical_id(FU_DEVICE(self->shadow_device)));
	fu_plugin_device_remove(plugin, FU_DEVICE(dev));

	/* no need to wait for more ports, cancel that right away */
	if (self->udev_timeout_id != 0) {
		g_source_remove(self->udev_timeout_id);
		self->udev_timeout_id = 0;
	}
}

static void
fu_mm_plugin_uninhibit_device(FuPlugin *plugin)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);
	g_autoptr(FuMmDevice) shadow_device = NULL;

	/* get the device removed from the plugin cache before uninhibiting */
	fu_mm_plugin_udev_device_removed(plugin);

	shadow_device = g_steal_pointer(&self->shadow_device);
	if (self->manager != NULL && shadow_device != NULL) {
		const gchar *inhibition_uid = fu_mm_device_get_inhibition_uid(shadow_device);
		g_debug("uninhibit modemmanager device with uid %s", inhibition_uid);
		mm_manager_uninhibit_device_sync(self->manager, inhibition_uid, NULL, NULL);
	}
}

static gboolean
fu_mm_plugin_udev_device_ports_timeout(gpointer user_data)
{
	FuPlugin *plugin = user_data;
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);
	FuMmDevice *dev;
	g_autoptr(GError) error = NULL;

	g_return_val_if_fail(self->shadow_device != NULL, G_SOURCE_REMOVE);
	self->udev_timeout_id = 0;

	dev = fu_plugin_cache_lookup(plugin,
				     fu_device_get_physical_id(FU_DEVICE(self->shadow_device)));
	if (dev != NULL) {
		if (!fu_device_probe(FU_DEVICE(dev), &error)) {
			g_debug("failed to probe MM device: %s", error->message);
		} else {
			fu_plugin_device_add(plugin, FU_DEVICE(dev));
		}
	}

	return G_SOURCE_REMOVE;
}

static void
fu_mm_plugin_udev_device_ports_timeout_reset(FuPlugin *plugin)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);

	g_return_if_fail(self->shadow_device != NULL);
	if (self->udev_timeout_id != 0)
		g_source_remove(self->udev_timeout_id);
	self->udev_timeout_id = g_timeout_add_seconds(FU_MM_UDEV_DEVICE_PORTS_TIMEOUT,
						      fu_mm_plugin_udev_device_ports_timeout,
						      plugin);
}

static gboolean
fu_mm_plugin_inhibit_device(FuPlugin *plugin, FuDevice *device, GError **error)
{
	const gchar *inhibition_uid;
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);
	g_autoptr(FuMmDevice) shadow_device = NULL;

	fu_mm_plugin_uninhibit_device(plugin);

	shadow_device = fu_mm_device_shadow_new(FU_MM_DEVICE(device));
	inhibition_uid = fu_mm_device_get_inhibition_uid(shadow_device);
	g_debug("inhibit modemmanager device with uid %s", inhibition_uid);
	if (!mm_manager_inhibit_device_sync(self->manager, inhibition_uid, NULL, error))
		return FALSE;

	/* setup shadow_device device info */
	self->shadow_device = g_steal_pointer(&shadow_device);

	/* uninhibit when device re creation is detected */
	if (fu_device_has_private_flag(device,
				       FU_MM_DEVICE_FLAG_UNINHIBIT_MM_AFTER_FASTBOOT_REBOOT)) {
		self->device_ready_uninhibit_manager = TRUE;
	} else {
		self->device_ready_uninhibit_manager = FALSE;
	}

	return TRUE;
}

static void
fu_mm_plugin_ensure_modem_power_inhibit(FuPlugin *plugin, FuDevice *device)
{
	if (g_file_test(MODEM_POWER_SYSFS_PATH, G_FILE_TEST_EXISTS)) {
		fu_device_inhibit(device,
				  "modem-power",
				  "The modem-power kernel driver cannot be used");
	} else {
		fu_device_uninhibit(device, "modem-power");
	}
}

static void
fu_mm_plugin_device_add(FuPlugin *plugin, MMObject *modem)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);
	const gchar *object_path = mm_object_get_path(modem);
	g_autoptr(FuMmDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	g_debug("added modem: %s", object_path);

	if (fu_plugin_cache_lookup(plugin, object_path) != NULL) {
		g_warning("MM device already added, ignoring");
		return;
	}
	dev = fu_mm_device_new(fu_plugin_get_context(plugin), self->manager, modem);
	if (!fu_device_setup(FU_DEVICE(dev), &error)) {
		g_debug("failed to probe MM device: %s", error->message);
		return;
	}
	fu_mm_plugin_ensure_modem_power_inhibit(plugin, FU_DEVICE(dev));
	fu_plugin_device_add(plugin, FU_DEVICE(dev));
	fu_plugin_cache_add(plugin, object_path, dev);
	fu_plugin_cache_add(plugin, fu_device_get_physical_id(FU_DEVICE(dev)), dev);
}

static void
fu_mm_plugin_device_added_cb(MMManager *manager, MMObject *modem, FuPlugin *plugin)
{
	fu_mm_plugin_device_add(plugin, modem);
}

static void
fu_mm_plugin_device_removed_cb(MMManager *manager, MMObject *modem, FuPlugin *plugin)
{
	const gchar *object_path = mm_object_get_path(modem);
	FuMmDevice *dev = fu_plugin_cache_lookup(plugin, object_path);
	MMModemFirmwareUpdateMethod update_methods = MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE;

	if (dev == NULL)
		return;
	g_debug("removed modem: %s", mm_object_get_path(modem));

#if MM_CHECK_VERSION(1, 19, 1)
	/* No information will be displayed during the upgrade process if the
	 * device is removed, the main reason is that device is "removed" from
	 * ModemManager, but it still exists in the system */
	update_methods =
	    MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU | MM_MODEM_FIRMWARE_UPDATE_METHOD_SAHARA;
#else
	update_methods = MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU;
#endif

	if (!(fu_mm_device_get_update_methods(FU_MM_DEVICE(dev)) & update_methods)) {
		fu_plugin_cache_remove(plugin, object_path);
		fu_plugin_device_remove(plugin, FU_DEVICE(dev));
	}
}

static void
fu_mm_plugin_teardown_manager(FuPlugin *plugin)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);

	if (self->manager_ready) {
		g_debug("ModemManager no longer available");
		g_signal_handlers_disconnect_by_func(self->manager,
						     G_CALLBACK(fu_mm_plugin_device_added_cb),
						     plugin);
		g_signal_handlers_disconnect_by_func(self->manager,
						     G_CALLBACK(fu_mm_plugin_device_removed_cb),
						     plugin);
		self->manager_ready = FALSE;
	}
}

static void
fu_mm_plugin_setup_manager(FuPlugin *plugin)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);
	const gchar *version = mm_manager_get_version(self->manager);
	GList *list;

	if (fu_version_compare(version, MM_REQUIRED_VERSION, FWUPD_VERSION_FORMAT_TRIPLET) < 0) {
		g_warning("ModemManager %s is available, but need at least %s",
			  version,
			  MM_REQUIRED_VERSION);
		return;
	}

	g_info("ModemManager %s is available", version);

	g_signal_connect(G_DBUS_OBJECT_MANAGER(self->manager),
			 "object-added",
			 G_CALLBACK(fu_mm_plugin_device_added_cb),
			 plugin);
	g_signal_connect(G_DBUS_OBJECT_MANAGER(self->manager),
			 "object-removed",
			 G_CALLBACK(fu_mm_plugin_device_removed_cb),
			 plugin);

	list = g_dbus_object_manager_get_objects(G_DBUS_OBJECT_MANAGER(self->manager));
	for (GList *l = list; l != NULL; l = g_list_next(l)) {
		MMObject *modem = MM_OBJECT(l->data);
		fu_mm_plugin_device_add(plugin, modem);
		g_object_unref(modem);
	}
	g_list_free(list);

	self->manager_ready = TRUE;
}

static void
fu_mm_plugin_name_owner_updated(FuPlugin *plugin)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);
	g_autofree gchar *name_owner = g_dbus_object_manager_client_get_name_owner(
	    G_DBUS_OBJECT_MANAGER_CLIENT(self->manager));
	if (name_owner != NULL)
		fu_mm_plugin_setup_manager(plugin);
	else
		fu_mm_plugin_teardown_manager(plugin);
}

static gboolean
fu_mm_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);
	g_signal_connect_swapped(MM_MANAGER(self->manager),
				 "notify::name-owner",
				 G_CALLBACK(fu_mm_plugin_name_owner_updated),
				 plugin);
	fu_mm_plugin_name_owner_updated(plugin);
	return TRUE;
}

static void
fu_mm_plugin_modem_power_changed_cb(GFileMonitor *monitor,
				    GFile *file,
				    GFile *other_file,
				    GFileMonitorEvent event_type,
				    gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN(user_data);
	GPtrArray *devices = fu_plugin_get_devices(plugin);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		fu_mm_plugin_ensure_modem_power_inhibit(plugin, device);
	}
}

static gboolean
fu_mm_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GFile) file = g_file_new_for_path(MODEM_POWER_SYSFS_PATH);

	connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL)
		return FALSE;
	self->manager = mm_manager_new_sync(connection,
					    G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
					    NULL,
					    error);
	if (self->manager == NULL)
		return FALSE;

	/* detect presence of unsupported modem-power driver */
	self->modem_power_monitor = g_file_monitor(file, G_FILE_MONITOR_NONE, NULL, error);
	if (self->modem_power_monitor == NULL)
		return FALSE;
	g_signal_connect(self->modem_power_monitor,
			 "changed",
			 G_CALLBACK(fu_mm_plugin_modem_power_changed_cb),
			 plugin);

	return TRUE;
}

static gboolean
fu_mm_plugin_detach(FuPlugin *plugin, FuDevice *device, FuProgress *progress, GError **error)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	/* inhibit device and track it inside the plugin, not bound to the
	 * lifetime of the FuMmDevice, because that object will only exist for
	 * as long as the ModemManager device exists, and inhibiting will
	 * implicitly remove the device from ModemManager. */
	if (self->shadow_device == NULL) {
		if (!fu_mm_plugin_inhibit_device(plugin, device, error))
			return FALSE;
	}

	/* reset */
	if (!fu_device_detach_full(device, progress, error)) {
		fu_mm_plugin_uninhibit_device(plugin);
		return FALSE;
	}

	/* note: wait for replug set by device if it really needs it */
	return TRUE;
}

static void
fu_mm_plugin_device_attach_finished(gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN(user_data);
	fu_mm_plugin_uninhibit_device(plugin);
}

static gboolean
fu_mm_plugin_attach(FuPlugin *plugin, FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	/* schedule device attach asynchronously, which is extremely important
	 * so that engine can setup the device "waiting" logic before the actual
	 * attach procedure happens (which will reset the module if it worked
	 * properly) */
	if (!fu_device_attach_full(device, progress, error))
		return FALSE;

	/* this signal will always be emitted asynchronously */
	g_signal_connect_swapped(FU_DEVICE(device),
				 "attach-finished",
				 G_CALLBACK(fu_mm_plugin_device_attach_finished),
				 plugin);

	return TRUE;
}

static void
fu_mm_plugin_shadow_device_added(FuPlugin *plugin, FuDevice *device)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);
	FuMmDevice *existing;
	const gchar *subsystem = fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device));
	const gchar *device_file = fu_udev_device_get_device_file(FU_UDEV_DEVICE(device));
	g_autoptr(FuMmDevice) dev = NULL;

	/* device re creation, uninhibit manager */
	if (self->device_ready_uninhibit_manager) {
		self->device_ready_uninhibit_manager = FALSE;
		fu_mm_plugin_uninhibit_device(plugin);
	}

	existing =
	    fu_plugin_cache_lookup(plugin,
				   fu_device_get_physical_id(FU_DEVICE(self->shadow_device)));
	if (existing != NULL) {
		/* add port to existing device */
		fu_mm_device_udev_add_port(existing, subsystem, device_file);
		fu_mm_plugin_udev_device_ports_timeout_reset(plugin);
		return;
	}

	/* create device and add to cache */
	dev = fu_mm_device_udev_new(fu_plugin_get_context(plugin),
				    self->manager,
				    self->shadow_device);
	fu_mm_device_udev_add_port(dev, subsystem, device_file);
	fu_plugin_cache_add(plugin,
			    fu_device_get_physical_id(FU_DEVICE(self->shadow_device)),
			    device);

	/* wait a bit before probing, in case more ports get added */
	fu_mm_plugin_udev_device_ports_timeout_reset(plugin);
}

static gboolean
fu_mm_plugin_backend_device_removed(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);

	if (self->shadow_device != NULL &&
	    g_strcmp0(fu_device_get_physical_id(device),
		      fu_device_get_physical_id(FU_DEVICE(self->shadow_device))) != 0) {
		fu_mm_plugin_udev_device_removed(plugin);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mm_plugin_backend_device_added(FuPlugin *plugin,
				  FuDevice *device,
				  FuProgress *progress,
				  GError **error)
{
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);
	FuDevice *device_tmp;

	/* interesting device? */
	if (!FU_IS_UDEV_DEVICE(device))
		return TRUE;

	/* ignore all events for ports not owned by our device */
	if (self->shadow_device != NULL &&
	    g_strcmp0(fu_device_get_physical_id(device),
		      fu_device_get_physical_id(FU_DEVICE(self->shadow_device))) != 0) {
		fu_mm_plugin_shadow_device_added(plugin, device);
	}

	/* set the latest udev device for the FuMmDevice that just appeared */
	device_tmp =
	    fu_plugin_cache_lookup(plugin, fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)));
	if (device_tmp == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s not added by ModemManager",
			    fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)));
		return FALSE;
	}
	fu_mm_device_set_udev_device(FU_MM_DEVICE(device_tmp), FU_UDEV_DEVICE(device));

	/* success */
	return TRUE;
}

static void
fu_mm_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	(void)fu_plugin_alloc_data(plugin, sizeof(FuModemManagerPlugin));
	fu_plugin_add_udev_subsystem(plugin, "tty");
	fu_plugin_add_udev_subsystem(plugin, "usbmisc");
	fu_plugin_add_udev_subsystem(plugin, "wwan");
}

static void
fu_mm_plugin_finalize(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuModemManagerPlugin *self = FU_MODEM_MANAGER_PLUGIN(plugin);

	fu_mm_plugin_uninhibit_device(plugin);

	if (self->udev_timeout_id)
		g_source_remove(self->udev_timeout_id);
	if (self->manager != NULL)
		g_object_unref(self->manager);
	if (self->modem_power_monitor != NULL)
		g_object_unref(self->modem_power_monitor);

	/* G_OBJECT_CLASS(fu_mm_plugin_parent_class)->finalize() not required as modular */
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->load = fu_mm_plugin_load;
	vfuncs->constructed = fu_mm_plugin_constructed;
	vfuncs->finalize = fu_mm_plugin_finalize;
	vfuncs->to_string = fu_mm_plugin_to_string;
	vfuncs->startup = fu_mm_plugin_startup;
	vfuncs->coldplug = fu_mm_plugin_coldplug;
	vfuncs->attach = fu_mm_plugin_attach;
	vfuncs->detach = fu_mm_plugin_detach;
	vfuncs->backend_device_added = fu_mm_plugin_backend_device_added;
	vfuncs->backend_device_removed = fu_mm_plugin_backend_device_removed;
}
