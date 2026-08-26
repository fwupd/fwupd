/*
 * Copyright 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBackend"

#include "config.h"

#include "fu-bluez-backend.h"
#include "fu-bluez-device.h"

struct _FuBluezBackend {
	FuBackend parent_instance;
	GDBusObjectManager *object_manager;
};

G_DEFINE_TYPE(FuBluezBackend, fu_bluez_backend, FU_TYPE_BACKEND)

static void
fu_bluez_backend_object_properties_changed(FuBluezBackend *self, GDBusProxy *proxy)
{
	const gchar *path = g_dbus_proxy_get_object_path(proxy);
	gboolean suitable;
	FuDevice *device_tmp;
	g_autoptr(FuBluezDevice) dev = NULL;
	g_autoptr(GVariant) val_connected = NULL;
	g_autoptr(GVariant) val_paired = NULL;
	g_autoptr(GVariant) val_services_resolved = NULL;

	/* device is suitable */
	val_connected = g_dbus_proxy_get_cached_property(proxy, "Connected");
	if (val_connected == NULL)
		return;
	val_paired = g_dbus_proxy_get_cached_property(proxy, "Paired");
	if (val_paired == NULL)
		return;
	val_services_resolved = g_dbus_proxy_get_cached_property(proxy, "ServicesResolved");
	if (val_services_resolved == NULL)
		return;

	suitable = fwupd_variant_get_boolean(val_connected) &&
		   fwupd_variant_get_boolean(val_paired) &&
		   fwupd_variant_get_boolean(val_services_resolved);

	/* is this an existing device we've previously added */
	device_tmp = fu_backend_lookup_by_id(FU_BACKEND(self), path);
	if (device_tmp != NULL) {
		if (suitable) {
			g_debug("ignoring suitable changed BlueZ device: %s", path);
			return;
		}
		g_debug("removing unsuitable BlueZ device: %s", path);
		fu_backend_device_removed(FU_BACKEND(self), device_tmp);
		return;
	}

	/* not paired and connected */
	if (!suitable) {
		g_debug("%s connected=%i, paired=%i, services resolved=%i, ignoring",
			path,
			fwupd_variant_get_boolean(val_connected),
			fwupd_variant_get_boolean(val_paired),
			fwupd_variant_get_boolean(val_services_resolved));
		return;
	}

	/* create device */
	dev = g_object_new(FU_TYPE_BLUEZ_DEVICE,
			   "backend-id",
			   path,
			   "object-manager",
			   self->object_manager,
			   "proxy",
			   proxy,
			   NULL);
	g_info("adding suitable BlueZ device: %s", path);
	fu_backend_device_added(FU_BACKEND(self), FU_DEVICE(dev));
}

static void
fu_bluez_backend_object_properties_changed_cb(GDBusProxy *proxy,
					      GVariant *changed_properties,
					      GStrv invalidated_properties,
					      FuBluezBackend *self)
{
	fu_bluez_backend_object_properties_changed(self, proxy);
}

static void
fu_bluez_backend_object_added(FuBluezBackend *self, GDBusObject *object)
{
	g_autoptr(GDBusInterface) iface = NULL;

	iface = g_dbus_object_get_interface(object, "org.bluez.Device1");
	if (iface == NULL)
		return;
	g_signal_connect(G_DBUS_INTERFACE(iface),
			 "g-properties-changed",
			 G_CALLBACK(fu_bluez_backend_object_properties_changed_cb),
			 self);
	fu_bluez_backend_object_properties_changed(self, G_DBUS_PROXY(iface));
}

static void
fu_bluez_backend_object_added_cb(GDBusObjectManager *manager,
				 GDBusObject *object,
				 FuBluezBackend *self)
{
	fu_bluez_backend_object_added(self, object);
}

static void
fu_bluez_backend_object_removed_cb(GDBusObjectManager *manager,
				   GDBusObject *object,
				   FuBluezBackend *self)
{
	const gchar *path = g_dbus_object_get_object_path(object);
	FuDevice *device_tmp;

	device_tmp = fu_backend_lookup_by_id(FU_BACKEND(self), path);
	if (device_tmp == NULL)
		return;
	g_info("removing BlueZ device: %s", path);
	fu_backend_device_removed(FU_BACKEND(self), device_tmp);
}

static gboolean
fu_bluez_backend_setup(FuBackend *backend,
		       FuBackendSetupFlags flags,
		       FuProgress *progress,
		       GError **error)
{
	FuBluezBackend *self = FU_BLUEZ_BACKEND(backend);
	FuContext *ctx = fu_backend_get_context(backend);
	GMainContext *main_ctx = fu_context_get_main_context(ctx);

	/* GDBusObjectManagerClient subscribes to the bus signals from ::init, and
	 * g_dbus_connection_signal_subscribe() binds the subscription to the
	 * thread-default main context of the *calling* thread. The async constructor
	 * runs ::init in a GTask worker thread, which has no thread-default context --
	 * so the subscription would be bound to the global default context and the
	 * device property changes would never be dispatched. Construct synchronously
	 * with @main_ctx pushed so that everything is bound to the context we iterate. */
	g_main_context_push_thread_default(main_ctx);
	self->object_manager = g_dbus_object_manager_client_new_for_bus_sync(
	    G_BUS_TYPE_SYSTEM,
	    G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_DO_NOT_AUTO_START,
	    "org.bluez",
	    "/",
	    NULL,
	    NULL,
	    NULL,
	    NULL,
	    error);
	g_main_context_pop_thread_default(main_ctx);

	if (self->object_manager == NULL)
		return FALSE;

	if (flags & FU_BACKEND_SETUP_FLAG_USE_HOTPLUG) {
		g_signal_connect(G_DBUS_OBJECT_MANAGER(self->object_manager),
				 "object-added",
				 G_CALLBACK(fu_bluez_backend_object_added_cb),
				 self);
		g_signal_connect(G_DBUS_OBJECT_MANAGER(self->object_manager),
				 "object-removed",
				 G_CALLBACK(fu_bluez_backend_object_removed_cb),
				 self);
	}
	return TRUE;
}

static gboolean
fu_bluez_backend_coldplug(FuBackend *backend, FuProgress *progress, GError **error)
{
	FuBluezBackend *self = FU_BLUEZ_BACKEND(backend);
	g_autolist(GDBusObject) objects = NULL;

	/* failed to set up */
	if (self->object_manager == NULL)
		return TRUE;
	objects = g_dbus_object_manager_get_objects(self->object_manager);
	for (GList *l = objects; l != NULL; l = l->next) {
		GDBusObject *object = G_DBUS_OBJECT(l->data);
		fu_bluez_backend_object_added(self, object);
	}
	return TRUE;
}

static void
fu_bluez_backend_finalize(GObject *object)
{
	FuBluezBackend *self = FU_BLUEZ_BACKEND(object);
	if (self->object_manager != NULL)
		g_object_unref(self->object_manager);
	G_OBJECT_CLASS(fu_bluez_backend_parent_class)->finalize(object);
}

static void
fu_bluez_backend_init(FuBluezBackend *self)
{
}

static void
fu_bluez_backend_class_init(FuBluezBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuBackendClass *backend_class = FU_BACKEND_CLASS(klass);

	object_class->finalize = fu_bluez_backend_finalize;
	backend_class->setup = fu_bluez_backend_setup;
	backend_class->coldplug = fu_bluez_backend_coldplug;
}

FuBackend *
fu_bluez_backend_new(FuContext *ctx)
{
	return FU_BACKEND(g_object_new(FU_TYPE_BLUEZ_BACKEND,
				       "name",
				       "bluez",
				       "context",
				       ctx,
				       "device-gtype",
				       FU_TYPE_BLUEZ_DEVICE,
				       NULL));
}
