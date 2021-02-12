/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuBackend"

#include "config.h"

#include "fu-bluez-device.h"
#include "fu-bluez-backend.h"

struct _FuBluezBackend {
	FuBackend		 parent_instance;
	GDBusObjectManager	*object_manager;
	GHashTable		*devices;	/* object_path : * FuDevice */
};

G_DEFINE_TYPE (FuBluezBackend, fu_bluez_backend, FU_TYPE_BACKEND)

static void
fu_bluez_backend_object_properties_changed (FuBluezBackend *self, GDBusProxy *proxy)
{
	const gchar *path = g_dbus_proxy_get_object_path (proxy);
	gboolean suitable;
	FuDevice *device_tmp;
	g_autoptr(FuBluezDevice) dev = NULL;
	g_autoptr(GVariant) val_connected = NULL;
	g_autoptr(GVariant) val_paired = NULL;

	/* device is suitable */
	val_connected = g_dbus_proxy_get_cached_property (proxy, "Connected");
	if (val_connected == NULL)
		return;
	val_paired = g_dbus_proxy_get_cached_property (proxy, "Paired");
	if (val_paired == NULL)
		return;
	suitable = g_variant_get_boolean (val_connected) &&
			g_variant_get_boolean (val_paired);

	/* is this an existing device we've previously added */
	device_tmp = g_hash_table_lookup (self->devices, path);
	if (device_tmp != NULL) {
		if (suitable) {
			g_debug ("ignoring suitable changed Bluez device: %s", path);
			return;
		}
		g_debug ("removing unsuitable Bluez device: %s", path);
		fu_backend_device_removed (FU_BACKEND (self), device_tmp);
		g_hash_table_remove (self->devices, path);
		return;
	}

	/* not paired and connected */
	if (!suitable)
		return;

	/* create device */
	dev = g_object_new (FU_TYPE_BLUEZ_DEVICE,
			    "object-manager", self->object_manager,
			    "proxy", proxy,
			    NULL);
	g_debug ("adding suitable Bluez device: %s", path);
	g_hash_table_insert (self->devices, g_strdup (path), g_object_ref (dev));
	fu_backend_device_added (FU_BACKEND (self), FU_DEVICE (dev));
}

static void
fu_bluez_backend_object_properties_changed_cb (GDBusProxy *proxy,
					       GVariant *changed_properties,
					       GStrv invalidated_properties,
					       FuBluezBackend *self)
{
	fu_bluez_backend_object_properties_changed (self, proxy);
}

static void
fu_bluez_backend_object_added (FuBluezBackend *self, GDBusObject *object)
{
	g_autoptr(GDBusInterface) iface = NULL;
	g_auto(GStrv) names = NULL;

	iface = g_dbus_object_get_interface (object, "org.bluez.Device1");
	if (iface == NULL)
		return;
	g_signal_connect (iface, "g-properties-changed",
			  G_CALLBACK (fu_bluez_backend_object_properties_changed_cb),
			  self);
	fu_bluez_backend_object_properties_changed (self, G_DBUS_PROXY (iface));
}

static void
fu_bluez_backend_object_added_cb (GDBusObjectManager *manager,
				  GDBusObject *object,
				  FuBluezBackend *self)
{
	fu_bluez_backend_object_added (self, object);
}

static void
fu_bluez_backend_object_removed_cb (GDBusObjectManager *manager,
				    GDBusObject *object,
				    FuBluezBackend *self)
{
	const gchar *path = g_dbus_object_get_object_path (object);
	FuDevice *device_tmp;

	device_tmp = g_hash_table_lookup (self->devices, path);
	if (device_tmp == NULL)
		return;
	g_debug ("removing Bluez device: %s", path);
	fu_backend_device_removed (FU_BACKEND (self), device_tmp);
	g_hash_table_remove (self->devices, path);
}

static gboolean
fu_bluez_backend_setup (FuBackend *backend, GError **error)
{
	FuBluezBackend *self = FU_BLUEZ_BACKEND (backend);

	self->object_manager = g_dbus_object_manager_client_new_for_bus_sync (
					G_BUS_TYPE_SYSTEM,
					G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
					"org.bluez",
					"/",
					NULL, NULL, NULL,
					NULL, error);
	if (self->object_manager == NULL)
		return FALSE;
	g_signal_connect (self->object_manager, "object-added",
			  G_CALLBACK (fu_bluez_backend_object_added_cb), self);
	g_signal_connect (self->object_manager, "object-removed",
			  G_CALLBACK (fu_bluez_backend_object_removed_cb), self);
	return TRUE;
}

static gboolean
fu_bluez_backend_coldplug (FuBackend *backend, GError **error)
{
	FuBluezBackend *self = FU_BLUEZ_BACKEND (backend);
	g_autolist(GDBusObject) objects = NULL;

	/* failed to set up */
	if (self->object_manager == NULL)
		return TRUE;
	objects = g_dbus_object_manager_get_objects (self->object_manager);
	for (GList *l = objects; l != NULL; l = l->next) {
		GDBusObject *object = G_DBUS_OBJECT (l->data);
		fu_bluez_backend_object_added (self, object);
	}
	return TRUE;
}

static void
fu_bluez_backend_finalize (GObject *object)
{
	FuBluezBackend *self = FU_BLUEZ_BACKEND (object);
	if (self->object_manager != NULL)
		g_object_unref (self->object_manager);
	g_hash_table_unref (self->devices);
	G_OBJECT_CLASS (fu_bluez_backend_parent_class)->finalize (object);
}

static void
fu_bluez_backend_init (FuBluezBackend *self)
{
	self->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free,
					       (GDestroyNotify) g_object_unref);
}

static void
fu_bluez_backend_class_init (FuBluezBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuBackendClass *klass_backend = FU_BACKEND_CLASS (klass);

	object_class->finalize = fu_bluez_backend_finalize;
	klass_backend->setup = fu_bluez_backend_setup;
	klass_backend->coldplug = fu_bluez_backend_coldplug;
	klass_backend->recoldplug = fu_bluez_backend_coldplug;
}

FuBackend *
fu_bluez_backend_new (void)
{
	return FU_BACKEND (g_object_new (FU_TYPE_BLUEZ_BACKEND, "name", "bluez", NULL));
}
