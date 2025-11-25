/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-backend.h"
#include "fu-mm-common.h"
#include "fu-mm-dfota-device.h"
#include "fu-mm-fastboot-device.h"
#include "fu-mm-fdl-device.h"
#include "fu-mm-firehose-device.h"
#include "fu-mm-mbim-device.h"
#include "fu-mm-mhi-qcdm-device.h"
#include "fu-mm-qcdm-device.h"
#include "fu-mm-qdu-mbim-device.h"
#include "fu-mm-qmi-device.h"

struct _FuMmBackend {
	FuBackend parent_instance;
	MMManager *manager;
	gboolean manager_ready;
	GFileMonitor *modem_power_monitor;
};

G_DEFINE_TYPE(FuMmBackend, fu_mm_backend, FU_TYPE_BACKEND)

/* out-of-tree modem-power driver is unsupported */
#define FU_MM_BACKEND_MODEM_POWER_SYSFS_PATH "/sys/class/modem-power"

static void
fu_mm_backend_to_string(FuBackend *backend, guint idt, GString *str)
{
	FuMmBackend *self = FU_MM_BACKEND(backend);
	fwupd_codec_string_append_bool(str, idt, "ManagerReady", self->manager_ready);
}

static void
fu_mm_backend_device_inhibit(FuMmBackend *self, FuMmDevice *device)
{
	const gchar *inhibition_uid = fu_mm_device_get_inhibition_uid(FU_MM_DEVICE(device));
	g_autoptr(GError) error_local = NULL;

	if (inhibition_uid == NULL)
		return;
	g_debug("inhibit modemmanager device with uid %s", inhibition_uid);
	if (!mm_manager_inhibit_device_sync(self->manager, inhibition_uid, NULL, &error_local))
		g_debug("ignoring: %s", error_local->message);
}

static void
fu_mm_backend_device_uninhibit(FuMmBackend *self, FuMmDevice *device)
{
	const gchar *inhibition_uid = fu_mm_device_get_inhibition_uid(FU_MM_DEVICE(device));
	g_autoptr(GError) error_local = NULL;

	if (inhibition_uid == NULL)
		return;
	g_debug("uninhibit modemmanager device with uid %s", inhibition_uid);
	if (!mm_manager_uninhibit_device_sync(self->manager, inhibition_uid, NULL, &error_local))
		g_debug("ignoring: %s", error_local->message);
}

static void
fu_mm_backend_device_inhibited_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	FuMmBackend *self = FU_MM_BACKEND(user_data);
	if (fu_mm_device_get_inhibited(FU_MM_DEVICE(device))) {
		fu_mm_backend_device_inhibit(self, FU_MM_DEVICE(device));
		return;
	}
	fu_mm_backend_device_uninhibit(self, FU_MM_DEVICE(device));
}

static FuDevice *
fu_mm_backend_probe_gtype(FuMmBackend *self, MMObject *omodem, GError **error)
{
	FuContext *ctx = fu_backend_get_context(FU_BACKEND(self));
	MMModemFirmware *modem_fw = mm_object_peek_modem_firmware(omodem);
	const gchar **device_ids;
	g_autofree gchar *device_ids_str = NULL;
	g_autoptr(MMFirmwareUpdateSettings) update_settings = NULL;

	/* use the instance IDs provided by ModemManager to find the correct GType */
	update_settings = mm_modem_firmware_get_update_settings(modem_fw);
	device_ids = mm_firmware_update_settings_get_device_ids(update_settings);
	if (device_ids == NULL || device_ids[0] == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "modem did not specify any device IDs");
		return NULL;
	}
	for (guint i = 0; device_ids[i] != NULL; i++) {
		g_autofree gchar *guid = fwupd_guid_hash_string(device_ids[i]);
		const gchar *gtypestr = fu_context_lookup_quirk_by_id(ctx, guid, FU_QUIRKS_GTYPE);
		if (gtypestr != NULL) {
			GType gtype = g_type_from_name(gtypestr);
			if (gtype == G_TYPE_INVALID) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "unknown GType name %s",
					    gtypestr);
				return NULL;
			}
			return g_object_new(gtype, "context", ctx, NULL);
		}
	}

	/* failed */
	device_ids_str = g_strjoinv(", ", (gchar **)device_ids);
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "no explicit GType for %s",
		    device_ids_str);
	return NULL;
}

static FuDevice *
fu_mm_backend_probe_gtype_fallback(FuMmBackend *self, MMObject *omodem, GError **error)
{
	MMModem *modem = mm_object_peek_modem(omodem);
	FuContext *ctx = fu_backend_get_context(FU_BACKEND(self));
	MMModemFirmware *modem_fw;
	MMModemFirmwareUpdateMethod update_methods;
	MMModemPortInfo *used_ports = NULL;
	guint n_used_ports = 0;
#if MM_CHECK_VERSION(1, 26, 0)
	MMModemPortInfo *ignored_ports = NULL;
	guint n_ignored_ports = 0;
#endif // MM_CHECK_VERSION(1, 26, 0)
	const gchar **device_ids;
	guint64 ports_bitmask = 0;
	GType gtype = G_TYPE_INVALID;
	g_autoptr(MMFirmwareUpdateSettings) update_settings = NULL;
	struct {
		GType gtype;
		MMModemPortType port_type;
		MMModemFirmwareUpdateMethod method;
	} map[] = {
	    {
		FU_TYPE_MM_QDU_MBIM_DEVICE,
		MM_MODEM_PORT_TYPE_MBIM,
		MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU,
	    },
	    {
		FU_TYPE_MM_MBIM_DEVICE,
		MM_MODEM_PORT_TYPE_MBIM,
		MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE | MM_MODEM_FIRMWARE_UPDATE_METHOD_SAHARA,
	    },
	    {
		FU_TYPE_MM_FASTBOOT_DEVICE,
		MM_MODEM_PORT_TYPE_AT,
		MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT,
	    },
	    {
		FU_TYPE_MM_QMI_DEVICE,
		MM_MODEM_PORT_TYPE_QMI,
		MM_MODEM_FIRMWARE_UPDATE_METHOD_QMI_PDC | MM_MODEM_FIRMWARE_UPDATE_METHOD_FASTBOOT,
	    },
	    {
		FU_TYPE_MM_QCDM_DEVICE,
		MM_MODEM_PORT_TYPE_QCDM,
		MM_MODEM_FIRMWARE_UPDATE_METHOD_MBIM_QDU,
	    },
	    {
		FU_TYPE_MM_MHI_QCDM_DEVICE,
		MM_MODEM_PORT_TYPE_QCDM,
		MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE,
	    },
	    {
		FU_TYPE_MM_QCDM_DEVICE,
		MM_MODEM_PORT_TYPE_QCDM,
		MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE | MM_MODEM_FIRMWARE_UPDATE_METHOD_SAHARA,
	    },
	    {
		FU_TYPE_MM_FIREHOSE_DEVICE,
		MM_MODEM_PORT_TYPE_AT,
		MM_MODEM_FIRMWARE_UPDATE_METHOD_FIREHOSE | MM_MODEM_FIRMWARE_UPDATE_METHOD_SAHARA,
	    },
	    {
		FU_TYPE_MM_FDL_DEVICE,
		MM_MODEM_PORT_TYPE_AT,
		MM_MODEM_FIRMWARE_UPDATE_METHOD_CINTERION_FDL,
	    },
	    {
		FU_TYPE_MM_DFOTA_DEVICE,
		MM_MODEM_PORT_TYPE_AT,
		MM_MODEM_FIRMWARE_UPDATE_METHOD_DFOTA,
	    },
	};

	modem_fw = mm_object_peek_modem_firmware(omodem);
	update_settings = mm_modem_firmware_get_update_settings(modem_fw);
	update_methods = mm_firmware_update_settings_get_method(update_settings);
	if (update_methods == MM_MODEM_FIRMWARE_UPDATE_METHOD_NONE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "does not support firmware updates");
		return NULL;
	}

	if (mm_modem_get_ports(modem, &used_ports, &n_used_ports)) {
		for (guint i = 0; i < n_used_ports; i++) {
			g_debug("found port %s: %s",
				used_ports[i].name,
				fu_mm_device_port_type_to_string(used_ports[i].type));
			FU_BIT_SET(ports_bitmask, used_ports[i].type);
		}
		mm_modem_port_info_array_free(used_ports, n_used_ports);
	}

#if MM_CHECK_VERSION(1, 26, 0)
	if (mm_modem_get_ignored_ports(modem, &ignored_ports, &n_ignored_ports)) {
		for (guint i = 0; i < n_ignored_ports; i++) {
			g_debug("found port %s: %s",
				ignored_ports[i].name,
				fu_mm_device_port_type_to_string(ignored_ports[i].type));
			FU_BIT_SET(ports_bitmask, ignored_ports[i].type);
		}
		mm_modem_port_info_array_free(ignored_ports, n_ignored_ports);
	}
#endif // MM_CHECK_VERSION(1, 26, 0)

	/* find the correct GType */
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		if (FU_BIT_IS_SET(ports_bitmask, map[i].port_type) &&
		    update_methods == map[i].method) {
			gtype = map[i].gtype;
			break;
		}
	}
	if (gtype == G_TYPE_INVALID) {
		g_autofree gchar *methods_str =
		    mm_modem_firmware_update_method_build_string_from_mask(update_methods);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "update method %s not supported",
			    methods_str);
		return NULL;
	}

	/* it's much better to be explicit, so ask the user to provide this information to us */
	device_ids = mm_firmware_update_settings_get_device_ids(update_settings);
	if (device_ids != NULL && device_ids[0] != NULL) {
		g_autofree gchar *device_ids_str = g_strjoinv(", ", (gchar **)device_ids);
#ifdef SUPPORTED_BUILD
		g_debug("no explicit GType for %s, falling back to %s",
			device_ids_str,
			g_type_name(gtype));
#else
		g_warning("no explicit GType for %s, falling back to %s",
			  device_ids_str,
			  g_type_name(gtype));
		g_warning("Please see "
			  "https://github.com/fwupd/fwupd/wiki/Daemon-Warning:-FuMmDevice-GType");
#endif
	}

	/* success */
	return g_object_new(gtype, "context", ctx, NULL);
}

static FuDevice *
fu_mm_backend_device_create_from_omodem(FuMmBackend *self, MMObject *omodem, GError **error)
{
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(GError) error_local = NULL;

	/* create device and probe */
	device = fu_mm_backend_probe_gtype(self, omodem, &error_local);
	if (device == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("ignoring, and trying legacy fallback: %s", error_local->message);
			device = fu_mm_backend_probe_gtype_fallback(self, omodem, error);
			if (device == NULL)
				return NULL;
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return NULL;
		}
	}
	if (!fu_mm_device_probe_from_omodem(FU_MM_DEVICE(device), omodem, error))
		return NULL;

	/* fastboot extra properties */
	if (FU_IS_MM_FASTBOOT_DEVICE(device)) {
		MMModemFirmware *modem_fw = mm_object_peek_modem_firmware(omodem);
		g_autoptr(MMFirmwareUpdateSettings) update_settings =
		    mm_modem_firmware_get_update_settings(modem_fw);
		const gchar *tmp = mm_firmware_update_settings_get_fastboot_at(update_settings);
		if (tmp == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "modem does not set fastboot command");
			return NULL;
		}
		fu_mm_fastboot_device_set_detach_at(FU_MM_FASTBOOT_DEVICE(device), tmp);
	}

	/* success */
	return g_steal_pointer(&device);
}

static void
fu_mm_backend_ensure_modem_power_inhibit(FuMmBackend *self, FuDevice *device)
{
	if (g_file_test(FU_MM_BACKEND_MODEM_POWER_SYSFS_PATH, G_FILE_TEST_EXISTS)) {
		fu_device_inhibit(device,
				  "modem-power",
				  "The modem-power kernel driver cannot be used");
	} else {
		fu_device_uninhibit(device, "modem-power");
	}
}

static void
fu_mm_backend_device_add(FuMmBackend *self, MMObject *omodem)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(FuDevice) device = NULL;

	device = fu_mm_backend_device_create_from_omodem(self, omodem, &error);
	if (device == NULL) {
		g_debug("ignoring: %s", error->message);
		return;
	}

	/* inhibit MmManager when required */
	g_signal_connect(device,
			 "notify::inhibited",
			 G_CALLBACK(fu_mm_backend_device_inhibited_notify_cb),
			 self);
	fu_mm_backend_ensure_modem_power_inhibit(self, device);
	fu_backend_device_added(FU_BACKEND(self), device);
}

static void
fu_mm_backend_device_added_cb(MMManager *manager, MMObject *omodem, FuMmBackend *self)
{
	FuDevice *device = fu_backend_lookup_by_id(FU_BACKEND(self), mm_object_get_path(omodem));
	if (device != NULL) {
		g_autoptr(GError) error_local = NULL;
		g_debug("modem came back, rescanning");
		if (!fu_mm_device_probe_from_omodem(FU_MM_DEVICE(device), omodem, &error_local))
			g_debug("ignoring: %s", error_local->message);
		// FIXME: perhaps need to mm_firmware_update_settings_get_fastboot_at()
	}
	fu_mm_backend_device_add(self, omodem);
}

static void
fu_mm_backend_device_removed_cb(MMManager *manager, MMObject *omodem, FuBackend *backend)
{
	FuDevice *device = fu_backend_lookup_by_id(backend, mm_object_get_path(omodem));
	if (device == NULL)
		return;
	if (fu_mm_device_get_inhibited(FU_MM_DEVICE(device))) {
		g_debug("inhibited modem %s, ignoring", fu_device_get_backend_id(device));
		return;
	}
	g_debug("removed modem: %s", fu_device_get_backend_id(device));
	fu_backend_device_removed(backend, device);
}

static void
fu_mm_backend_modem_power_changed_cb(GFileMonitor *monitor,
				     GFile *file,
				     GFile *other_file,
				     GFileMonitorEvent event_type,
				     gpointer user_data)
{
	FuMmBackend *self = FU_MM_BACKEND(user_data);
	GPtrArray *devices = fu_backend_get_devices(FU_BACKEND(self));
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		fu_mm_backend_ensure_modem_power_inhibit(self, device);
	}
}

static gboolean
fu_mm_backend_setup(FuBackend *backend,
		    FuBackendSetupFlags flags,
		    FuProgress *progress,
		    GError **error)
{
	FuMmBackend *self = FU_MM_BACKEND(backend);
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GFile) file = g_file_new_for_path(FU_MM_BACKEND_MODEM_POWER_SYSFS_PATH);

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
			 G_CALLBACK(fu_mm_backend_modem_power_changed_cb),
			 self);

	/* success */
	return TRUE;
}

static void
fu_mm_backend_teardown_manager(FuMmBackend *self)
{
	if (self->manager_ready) {
		g_debug("ModemManager no longer available");
		g_signal_handlers_disconnect_by_func(self->manager,
						     G_CALLBACK(fu_mm_backend_device_added_cb),
						     self);
		g_signal_handlers_disconnect_by_func(self->manager,
						     G_CALLBACK(fu_mm_backend_device_removed_cb),
						     self);
		self->manager_ready = FALSE;
	}
}

static void
fu_mm_backend_setup_manager(FuMmBackend *self)
{
	const gchar *version = mm_manager_get_version(self->manager);
	g_autolist(MMObject) list = NULL;

	if (fu_version_compare(version, MM_REQUIRED_VERSION, FWUPD_VERSION_FORMAT_TRIPLET) < 0) {
		g_warning("ModemManager %s is available, but need at least %s",
			  version,
			  MM_REQUIRED_VERSION);
		return;
	}

	g_info("ModemManager %s is available", version);
	g_signal_connect(G_DBUS_OBJECT_MANAGER(self->manager),
			 "object-added",
			 G_CALLBACK(fu_mm_backend_device_added_cb),
			 self);
	g_signal_connect(G_DBUS_OBJECT_MANAGER(self->manager),
			 "object-removed",
			 G_CALLBACK(fu_mm_backend_device_removed_cb),
			 self);

	list = g_dbus_object_manager_get_objects(G_DBUS_OBJECT_MANAGER(self->manager));
	for (GList *l = list; l != NULL; l = g_list_next(l)) {
		MMObject *modem = MM_OBJECT(l->data);
		fu_mm_backend_device_add(self, modem);
	}

	self->manager_ready = TRUE;
}

static void
fu_mm_backend_name_owner_changed(FuMmBackend *self)
{
	g_autofree gchar *name_owner = g_dbus_object_manager_client_get_name_owner(
	    G_DBUS_OBJECT_MANAGER_CLIENT(self->manager));
	if (name_owner != NULL)
		fu_mm_backend_setup_manager(self);
	else
		fu_mm_backend_teardown_manager(self);
}

static void
fu_mm_backend_name_owner_notify_cb(MMManager *manager, GParamSpec *pspec, FuMmBackend *self)
{
	fu_mm_backend_name_owner_changed(self);
}

static gboolean
fu_mm_backend_coldplug(FuBackend *backend, FuProgress *progress, GError **error)
{
	FuMmBackend *self = FU_MM_BACKEND(backend);
	g_signal_connect(MM_MANAGER(self->manager),
			 "notify::name-owner",
			 G_CALLBACK(fu_mm_backend_name_owner_notify_cb),
			 self);
	fu_mm_backend_name_owner_changed(self);
	return TRUE;
}

static void
fu_mm_backend_init(FuMmBackend *self)
{
}

static void
fu_mm_backend_finalize(GObject *object)
{
	FuMmBackend *self = FU_MM_BACKEND(object);
	if (self->manager != NULL)
		g_object_unref(self->manager);
	if (self->modem_power_monitor != NULL)
		g_object_unref(self->modem_power_monitor);
	G_OBJECT_CLASS(fu_mm_backend_parent_class)->finalize(object);
}

static void
fu_mm_backend_class_init(FuMmBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuBackendClass *backend_class = FU_BACKEND_CLASS(klass);
	object_class->finalize = fu_mm_backend_finalize;
	backend_class->to_string = fu_mm_backend_to_string;
	backend_class->setup = fu_mm_backend_setup;
	backend_class->coldplug = fu_mm_backend_coldplug;
}

FuBackend *
fu_mm_backend_new(FuContext *ctx)
{
	return g_object_new(FU_TYPE_MM_BACKEND, "name", "modem-manager", "context", ctx, NULL);
}
