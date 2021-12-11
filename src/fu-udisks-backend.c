/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuBackend"

#include "config.h"

#include "fu-block-device.h"
#include "fu-udisks-backend.h"

struct _FuUdisksBackend {
	FuBackend parent_instance;
	GDBusObjectManager *object_manager;
};

G_DEFINE_TYPE(FuUdisksBackend, fu_udisks_backend, FU_TYPE_BACKEND)

#define FU_UDISKS_BACKEND_TIMEOUT 1500 /* ms */

static void
fu_udisks_backend_object_properties_changed(FuUdisksBackend *self, GDBusProxy *proxy)
{
	const gchar *path = g_dbus_proxy_get_object_path(proxy);
	gboolean suitable;
	FuDevice *device_tmp;
	g_autofree const gchar **mountpoints = NULL;
	g_autoptr(FuBlockDevice) dev = NULL;
	g_autoptr(GDBusProxy) proxy_fs = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val_device = NULL;
	g_autoptr(GVariant) val_hint_ignore = NULL;
	g_autoptr(GVariant) val_hint_system = NULL;
	g_autoptr(GVariant) val_id_label = NULL;
	g_autoptr(GVariant) val_id_type = NULL;
	g_autoptr(GVariant) val_id_uuid = NULL;
	g_autoptr(GVariant) val_mountpoints = NULL;

	/* device is suitable */
	val_hint_ignore = g_dbus_proxy_get_cached_property(proxy, "HintIgnore");
	if (val_hint_ignore == NULL) {
		g_warning("no HintIgnore for %s", g_dbus_proxy_get_object_path(proxy));
		return;
	}
	val_hint_system = g_dbus_proxy_get_cached_property(proxy, "HintSystem");
	if (val_hint_system == NULL) {
		g_warning("no HintSystem for %s", g_dbus_proxy_get_object_path(proxy));
		return;
	}
	val_id_type = g_dbus_proxy_get_cached_property(proxy, "IdType");
	if (val_id_type == NULL) {
		g_warning("no IdType for %s", g_dbus_proxy_get_object_path(proxy));
		return;
	}
	val_id_uuid = g_dbus_proxy_get_cached_property(proxy, "IdUUID");
	if (val_id_uuid == NULL) {
		g_warning("no IdUUID for %s", g_dbus_proxy_get_object_path(proxy));
		return;
	}
	val_id_label = g_dbus_proxy_get_cached_property(proxy, "IdLabel");
	if (val_id_label == NULL) {
		g_warning("no IdLabel for %s", g_dbus_proxy_get_object_path(proxy));
		return;
	}
	val_device = g_dbus_proxy_get_cached_property(proxy, "Device");
	if (val_device == NULL) {
		g_warning("no Device for %s", g_dbus_proxy_get_object_path(proxy));
		return;
	}
	suitable = !g_variant_get_boolean(val_hint_ignore) &&
		   !g_variant_get_boolean(val_hint_system) &&
		   g_strcmp0(g_variant_get_string(val_id_type, NULL), "vfat") == 0;

	/* is this an existing device we've previously added */
	device_tmp = fu_backend_lookup_by_id(FU_BACKEND(self), path);
	if (device_tmp != NULL) {
		if (suitable) {
			g_debug("ignoring suitable changed UDisks device: %s", path);
			return;
		}
		g_debug("removing unsuitable UDisks device: %s", path);
		fu_backend_device_removed(FU_BACKEND(self), device_tmp);
		return;
	}

	/* need to get MountPoints from .Filesystem and set as the logical-id */
	proxy_fs = g_dbus_proxy_new_sync(g_dbus_proxy_get_connection(proxy),
					 G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
					 NULL,
					 g_dbus_proxy_get_name(proxy),
					 g_dbus_proxy_get_object_path(proxy),
					 "org.freedesktop.UDisks2.Filesystem",
					 NULL,
					 &error);
	if (proxy_fs == NULL) {
		g_warning("no FS proxy: %s", error->message);
		return;
	}

	/* device is suitable */
	val_mountpoints = g_dbus_proxy_get_cached_property(proxy_fs, "MountPoints");
	if (val_mountpoints != NULL)
		mountpoints = g_variant_get_bytestring_array(val_mountpoints, NULL);
	if (mountpoints == NULL || mountpoints[0] == NULL)
		suitable = FALSE;

	/* not okay */
	if (!suitable) {
		const gchar *id_type = g_variant_get_string(val_id_type, NULL);
		g_debug("%s not a suitable device: %s, is-system:%i, ignore:%i, mountpoint:%s",
			g_dbus_proxy_get_object_path(proxy),
			id_type != NULL && id_type[0] != '\0' ? id_type : "unknown",
			g_variant_get_boolean(val_hint_system),
			g_variant_get_boolean(val_hint_ignore),
			mountpoints != NULL ? mountpoints[0] : "none");
		return;
	}

	/* create device */
	dev = g_object_new(FU_TYPE_BLOCK_DEVICE,
			   "backend-id",
			   path,
			   "physical-id",
			   g_variant_get_bytestring(val_device),
			   "label",
			   g_variant_get_string(val_id_label, NULL),
			   "uuid",
			   g_variant_get_string(val_id_uuid, NULL),
			   "logical-id",
			   mountpoints[0],
			   NULL);
	g_debug("adding suitable UDisks device: %s", path);
	fu_backend_device_added(FU_BACKEND(self), FU_DEVICE(dev));
}

static void
fu_udisks_backend_object_properties_changed_cb(GDBusProxy *proxy,
					       GVariant *changed_properties,
					       GStrv invalidated_properties,
					       FuUdisksBackend *self)
{
	fu_udisks_backend_object_properties_changed(self, proxy);
}

static void
fu_udisks_backend_object_added(FuUdisksBackend *self, GDBusObject *object)
{
	g_autoptr(GDBusInterface) iface = NULL;

	iface = g_dbus_object_get_interface(object, "org.freedesktop.UDisks2.Block");
	if (iface == NULL) {
		g_debug("%s has no block interface", g_dbus_object_get_object_path(object));
		return;
	}
	g_signal_connect(iface,
			 "g-properties-changed",
			 G_CALLBACK(fu_udisks_backend_object_properties_changed_cb),
			 self);
	fu_udisks_backend_object_properties_changed(self, G_DBUS_PROXY(iface));
}

static void
fu_udisks_backend_object_added_cb(GDBusObjectManager *manager,
				  GDBusObject *object,
				  FuUdisksBackend *self)
{
	fu_udisks_backend_object_added(self, object);
}

static void
fu_udisks_backend_object_removed_cb(GDBusObjectManager *manager,
				    GDBusObject *object,
				    FuUdisksBackend *self)
{
	const gchar *path = g_dbus_object_get_object_path(object);
	FuDevice *device_tmp;

	device_tmp = fu_backend_lookup_by_id(FU_BACKEND(self), path);
	if (device_tmp == NULL)
		return;
	g_debug("removing UDisks device: %s", path);
	fu_backend_device_removed(FU_BACKEND(self), device_tmp);
}

typedef struct {
	GDBusObjectManager *object_manager;
	GMainLoop *loop;
	GError **error;
	GCancellable *cancellable;
	guint timeout_id;
} FuUdisksBackendHelper;

static void
fu_udisks_backend_helper_free(FuUdisksBackendHelper *helper)
{
	if (helper->object_manager != NULL)
		g_object_unref(helper->object_manager);
	if (helper->timeout_id != 0)
		g_source_remove(helper->timeout_id);
	g_cancellable_cancel(helper->cancellable);
	g_main_loop_unref(helper->loop);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUdisksBackendHelper, fu_udisks_backend_helper_free)

static void
fu_udisks_backend_connect_cb(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	FuUdisksBackendHelper *helper = (FuUdisksBackendHelper *)user_data;
	helper->object_manager =
	    g_dbus_object_manager_client_new_for_bus_finish(res, helper->error);
	g_main_loop_quit(helper->loop);
}

static gboolean
fu_udisks_backend_timeout_cb(gpointer user_data)
{
	FuUdisksBackendHelper *helper = (FuUdisksBackendHelper *)user_data;
	g_cancellable_cancel(helper->cancellable);
	helper->timeout_id = 0;
	return G_SOURCE_REMOVE;
}

static gboolean
fu_udisks_backend_setup(FuBackend *backend, GError **error)
{
	FuUdisksBackend *self = FU_UDISKS_BACKEND(backend);
	g_autoptr(FuUdisksBackendHelper) helper = g_new0(FuUdisksBackendHelper, 1);

	helper->error = error;
	helper->loop = g_main_loop_new(NULL, FALSE);
	helper->cancellable = g_cancellable_new();
	helper->timeout_id =
	    g_timeout_add(FU_UDISKS_BACKEND_TIMEOUT, fu_udisks_backend_timeout_cb, helper);
	g_dbus_object_manager_client_new_for_bus(G_BUS_TYPE_SYSTEM,
						 G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
						 "org.freedesktop.UDisks2",
						 "/org/freedesktop/UDisks2",
						 NULL,
						 NULL,
						 NULL,
						 helper->cancellable,
						 fu_udisks_backend_connect_cb,
						 helper);
	g_main_loop_run(helper->loop);
	if (helper->object_manager == NULL)
		return FALSE;
	self->object_manager = g_steal_pointer(&helper->object_manager);

	g_signal_connect(self->object_manager,
			 "object-added",
			 G_CALLBACK(fu_udisks_backend_object_added_cb),
			 self);
	g_signal_connect(self->object_manager,
			 "object-removed",
			 G_CALLBACK(fu_udisks_backend_object_removed_cb),
			 self);
	return TRUE;
}

static gboolean
fu_udisks_backend_coldplug(FuBackend *backend, GError **error)
{
	FuUdisksBackend *self = FU_UDISKS_BACKEND(backend);
	g_autolist(GDBusObject) objects = NULL;

	/* failed to set up */
	if (self->object_manager == NULL)
		return TRUE;
	objects = g_dbus_object_manager_get_objects(self->object_manager);
	for (GList *l = objects; l != NULL; l = l->next) {
		GDBusObject *object = G_DBUS_OBJECT(l->data);
		if (g_strcmp0(g_dbus_object_get_object_path(object),
			      "/org/freedesktop/UDisks2/Manager") == 0)
			continue;
		fu_udisks_backend_object_added(self, object);
	}
	return TRUE;
}

static void
fu_udisks_backend_finalize(GObject *object)
{
	FuUdisksBackend *self = FU_UDISKS_BACKEND(object);
	if (self->object_manager != NULL)
		g_object_unref(self->object_manager);
	G_OBJECT_CLASS(fu_udisks_backend_parent_class)->finalize(object);
}

static void
fu_udisks_backend_init(FuUdisksBackend *self)
{
}

static void
fu_udisks_backend_class_init(FuUdisksBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuBackendClass *klass_backend = FU_BACKEND_CLASS(klass);

	object_class->finalize = fu_udisks_backend_finalize;
	klass_backend->setup = fu_udisks_backend_setup;
	klass_backend->coldplug = fu_udisks_backend_coldplug;
}

FuBackend *
fu_udisks_backend_new(void)
{
	return FU_BACKEND(g_object_new(FU_TYPE_UDISKS_BACKEND, "name", "udisks", NULL));
}
