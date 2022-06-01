/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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

#define FU_BLUEZ_BACKEND_TIMEOUT 1500 /* ms */

static void
fu_bluez_backend_object_properties_changed(FuBluezBackend *self, GDBusProxy *proxy)
{
	const gchar *path = g_dbus_proxy_get_object_path(proxy);
	gboolean suitable;
	FuDevice *device_tmp;
	g_autoptr(FuBluezDevice) dev = NULL;
	g_autoptr(GVariant) val_connected = NULL;
	g_autoptr(GVariant) val_paired = NULL;

	/* device is suitable */
	val_connected = g_dbus_proxy_get_cached_property(proxy, "Connected");
	if (val_connected == NULL)
		return;
	val_paired = g_dbus_proxy_get_cached_property(proxy, "Paired");
	if (val_paired == NULL)
		return;
	suitable = g_variant_get_boolean(val_connected) && g_variant_get_boolean(val_paired);

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
	if (!suitable)
		return;

	/* create device */
	dev = g_object_new(FU_TYPE_BLUEZ_DEVICE,
			   "backend-id",
			   path,
			   "object-manager",
			   self->object_manager,
			   "proxy",
			   proxy,
			   NULL);
	g_debug("adding suitable BlueZ device: %s", path);
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
	g_debug("removing BlueZ device: %s", path);
	fu_backend_device_removed(FU_BACKEND(self), device_tmp);
}

typedef struct {
	GDBusObjectManager *object_manager;
	GMainLoop *loop;
	GError **error;
	GCancellable *cancellable;
	guint timeout_id;
} FuBluezBackendHelper;

static void
fu_bluez_backend_helper_free(FuBluezBackendHelper *helper)
{
	if (helper->object_manager != NULL)
		g_object_unref(helper->object_manager);
	if (helper->timeout_id != 0)
		g_source_remove(helper->timeout_id);
	g_cancellable_cancel(helper->cancellable);
	g_main_loop_unref(helper->loop);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuBluezBackendHelper, fu_bluez_backend_helper_free)

static void
fu_bluez_backend_connect_cb(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	FuBluezBackendHelper *helper = (FuBluezBackendHelper *)user_data;
	helper->object_manager =
	    g_dbus_object_manager_client_new_for_bus_finish(res, helper->error);
	g_main_loop_quit(helper->loop);
}

static gboolean
fu_bluez_backend_timeout_cb(gpointer user_data)
{
	FuBluezBackendHelper *helper = (FuBluezBackendHelper *)user_data;
	g_cancellable_cancel(helper->cancellable);
	helper->timeout_id = 0;
	return G_SOURCE_REMOVE;
}

static gboolean
fu_bluez_backend_setup(FuBackend *backend, FuProgress *progress, GError **error)
{
	FuBluezBackend *self = FU_BLUEZ_BACKEND(backend);
	g_autoptr(FuBluezBackendHelper) helper = g_new0(FuBluezBackendHelper, 1);

	/* in some circumstances the bluez daemon will just hang... do not wait
	 * forever and make fwupd startup also fail */
	helper->error = error;
	helper->loop = g_main_loop_new(NULL, FALSE);
	helper->cancellable = g_cancellable_new();
	helper->timeout_id =
	    g_timeout_add(FU_BLUEZ_BACKEND_TIMEOUT, fu_bluez_backend_timeout_cb, helper);
	g_dbus_object_manager_client_new_for_bus(G_BUS_TYPE_SYSTEM,
						 G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
						 "org.bluez",
						 "/",
						 NULL,
						 NULL,
						 NULL,
						 helper->cancellable,
						 fu_bluez_backend_connect_cb,
						 helper);
	g_main_loop_run(helper->loop);
	if (helper->object_manager == NULL)
		return FALSE;
	self->object_manager = g_steal_pointer(&helper->object_manager);

	g_signal_connect(G_DBUS_OBJECT_MANAGER(self->object_manager),
			 "object-added",
			 G_CALLBACK(fu_bluez_backend_object_added_cb),
			 self);
	g_signal_connect(G_DBUS_OBJECT_MANAGER(self->object_manager),
			 "object-removed",
			 G_CALLBACK(fu_bluez_backend_object_removed_cb),
			 self);
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
	FuBackendClass *klass_backend = FU_BACKEND_CLASS(klass);

	object_class->finalize = fu_bluez_backend_finalize;
	klass_backend->setup = fu_bluez_backend_setup;
	klass_backend->coldplug = fu_bluez_backend_coldplug;
}

FuBackend *
fu_bluez_backend_new(void)
{
	return FU_BACKEND(g_object_new(FU_TYPE_BLUEZ_BACKEND, "name", "bluez", NULL));
}
