/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuBackend"

#include "config.h"

#include <fwupdplugin.h>

#include <gudev/gudev.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-udev-backend.h"

struct _FuUdevBackend {
	FuBackend parent_instance;
	GUdevClient *gudev_client;
	GHashTable *changed_idle_ids; /* sysfs:FuUdevBackendHelper */
};

G_DEFINE_TYPE(FuUdevBackend, fu_udev_backend, FU_TYPE_BACKEND)

static void
fu_udev_backend_device_add(FuUdevBackend *self, GUdevDevice *udev_device)
{
	FuContext *ctx = fu_backend_get_context(FU_BACKEND(self));
	GType gtype = FU_TYPE_UDEV_DEVICE;
	g_autoptr(FuUdevDevice) device = NULL;
	g_autoptr(GPtrArray) possible_plugins = NULL;
	struct {
		const gchar *subsystem;
		GType gtype;
	} subsystem_gtype_map[] = {{"mei", FU_TYPE_MEI_DEVICE},
				   {"i2c", FU_TYPE_I2C_DEVICE},
				   {"i2c-dev", FU_TYPE_I2C_DEVICE},
				   {NULL, G_TYPE_INVALID}};

	/* create the correct object depending on the subsystem */
	for (guint i = 0; subsystem_gtype_map[i].gtype != G_TYPE_INVALID; i++) {
		if (g_strcmp0(g_udev_device_get_subsystem(udev_device),
			      subsystem_gtype_map[i].subsystem) == 0) {
			gtype = subsystem_gtype_map[i].gtype;
			break;
		}
	}

	/* success */
	device = g_object_new(gtype,
			      "context",
			      fu_backend_get_context(FU_BACKEND(self)),
			      "udev-device",
			      udev_device,
			      NULL);

	/* notify plugins using fu_plugin_add_udev_subsystem() */
	possible_plugins =
	    fu_context_get_plugin_names_for_udev_subsystem(ctx,
							   g_udev_device_get_subsystem(udev_device),
							   NULL);
	if (possible_plugins != NULL) {
		for (guint i = 0; i < possible_plugins->len; i++) {
			const gchar *plugin_name = g_ptr_array_index(possible_plugins, i);
			fu_device_add_possible_plugin(FU_DEVICE(device), plugin_name);
		}
	}

	/* success */
	fu_backend_device_added(FU_BACKEND(self), FU_DEVICE(device));
}

static void
fu_udev_backend_device_remove(FuUdevBackend *self, GUdevDevice *udev_device)
{
	FuDevice *device_tmp;

	/* find the device we enumerated */
	device_tmp =
	    fu_backend_lookup_by_id(FU_BACKEND(self), g_udev_device_get_sysfs_path(udev_device));
	if (device_tmp != NULL) {
		g_debug("UDEV %s removed", g_udev_device_get_sysfs_path(udev_device));
		fu_backend_device_removed(FU_BACKEND(self), device_tmp);
	}
}

typedef struct {
	FuUdevBackend *self;
	FuDevice *device;
	guint idle_id;
} FuUdevBackendHelper;

static void
fu_udev_backend_changed_helper_free(FuUdevBackendHelper *helper)
{
	if (helper->idle_id != 0)
		g_source_remove(helper->idle_id);
	g_object_unref(helper->self);
	g_object_unref(helper->device);
	g_free(helper);
}

static FuUdevBackendHelper *
fu_udev_backend_changed_helper_new(FuUdevBackend *self, FuDevice *device)
{
	FuUdevBackendHelper *helper = g_new0(FuUdevBackendHelper, 1);
	helper->self = g_object_ref(self);
	helper->device = g_object_ref(device);
	return helper;
}

static gboolean
fu_udev_backend_device_changed_cb(gpointer user_data)
{
	FuUdevBackendHelper *helper = (FuUdevBackendHelper *)user_data;
	fu_backend_device_changed(FU_BACKEND(helper->self), helper->device);
	helper->idle_id = 0;
	g_hash_table_remove(helper->self->changed_idle_ids,
			    fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(helper->device)));
	return FALSE;
}

static void
fu_udev_backend_device_changed(FuUdevBackend *self, GUdevDevice *udev_device)
{
	const gchar *sysfs_path = g_udev_device_get_sysfs_path(udev_device);
	FuUdevBackendHelper *helper;
	FuDevice *device_tmp;

	/* not a device we enumerated */
	device_tmp = fu_backend_lookup_by_id(FU_BACKEND(self), sysfs_path);
	if (device_tmp == NULL)
		return;

	/* run all plugins, with per-device rate limiting */
	if (g_hash_table_remove(self->changed_idle_ids, sysfs_path)) {
		g_debug("re-adding rate-limited timeout for %s", sysfs_path);
	} else {
		g_debug("adding rate-limited timeout for %s", sysfs_path);
	}
	helper = fu_udev_backend_changed_helper_new(self, device_tmp);
	helper->idle_id = g_timeout_add(500, fu_udev_backend_device_changed_cb, helper);
	g_hash_table_insert(self->changed_idle_ids, g_strdup(sysfs_path), helper);
}

static void
fu_udev_backend_uevent_cb(GUdevClient *gudev_client,
			  const gchar *action,
			  GUdevDevice *udev_device,
			  FuUdevBackend *self)
{
	if (g_strcmp0(action, "add") == 0) {
		fu_udev_backend_device_add(self, udev_device);
		return;
	}
	if (g_strcmp0(action, "remove") == 0) {
		fu_udev_backend_device_remove(self, udev_device);
		return;
	}
	if (g_strcmp0(action, "change") == 0) {
		fu_udev_backend_device_changed(self, udev_device);
		return;
	}
}

static void
fu_udev_backend_coldplug_subsystem(FuUdevBackend *self,
				   const gchar *subsystem,
				   FuProgress *progress)
{
	g_autolist(GObject) devices = NULL;

	devices = g_udev_client_query_by_subsystem(self->gudev_client, subsystem);
	g_debug("%u devices with subsystem %s", g_list_length(devices), subsystem);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_name(progress, subsystem);
	fu_progress_set_steps(progress, g_list_length(devices));
	for (GList *l = devices; l != NULL; l = l->next) {
		GUdevDevice *udev_device = l->data;
		fu_progress_set_name(fu_progress_get_child(progress),
				     g_udev_device_get_sysfs_path(udev_device));
		fu_udev_backend_device_add(self, udev_device);
		fu_progress_step_done(progress);
	}
}

static gboolean
fu_udev_backend_coldplug(FuBackend *backend, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_backend_get_context(backend);
	FuUdevBackend *self = FU_UDEV_BACKEND(backend);
	g_autoptr(GPtrArray) udev_subsystems = fu_context_get_udev_subsystems(ctx);

	/* udev watches can only be set up in _init() so set up client now */
	if (udev_subsystems->len > 0) {
		g_auto(GStrv) subsystems = g_new0(gchar *, udev_subsystems->len + 1);
		for (guint i = 0; i < udev_subsystems->len; i++) {
			const gchar *subsystem = g_ptr_array_index(udev_subsystems, i);
			subsystems[i] = g_strdup(subsystem);
		}
		self->gudev_client = g_udev_client_new((const gchar *const *)subsystems);
		g_signal_connect(G_UDEV_CLIENT(self->gudev_client),
				 "uevent",
				 G_CALLBACK(fu_udev_backend_uevent_cb),
				 self);
	}

	/* get all devices of class */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, udev_subsystems->len);
	for (guint i = 0; i < udev_subsystems->len; i++) {
		const gchar *subsystem = g_ptr_array_index(udev_subsystems, i);
		fu_udev_backend_coldplug_subsystem(self,
						   subsystem,
						   fu_progress_get_child(progress));
		fu_progress_step_done(progress);
	}

	return TRUE;
}

static void
fu_udev_backend_finalize(GObject *object)
{
	FuUdevBackend *self = FU_UDEV_BACKEND(object);
	if (self->gudev_client != NULL)
		g_object_unref(self->gudev_client);
	g_hash_table_unref(self->changed_idle_ids);
	G_OBJECT_CLASS(fu_udev_backend_parent_class)->finalize(object);
}

static void
fu_udev_backend_init(FuUdevBackend *self)
{
	self->changed_idle_ids =
	    g_hash_table_new_full(g_str_hash,
				  g_str_equal,
				  g_free,
				  (GDestroyNotify)fu_udev_backend_changed_helper_free);
}

static void
fu_udev_backend_class_init(FuUdevBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuBackendClass *klass_backend = FU_BACKEND_CLASS(klass);
	object_class->finalize = fu_udev_backend_finalize;
	klass_backend->coldplug = fu_udev_backend_coldplug;
}

FuBackend *
fu_udev_backend_new(FuContext *ctx)
{
	return FU_BACKEND(g_object_new(FU_TYPE_UDEV_BACKEND, "name", "udev", "context", ctx, NULL));
}
