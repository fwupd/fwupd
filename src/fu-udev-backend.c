/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuBackend"

#include "config.h"

#include <gudev/gudev.h>

#include "fu-udev-device.h"
#include "fu-udev-backend.h"

struct _FuUdevBackend {
	FuBackend		 parent_instance;
	GUdevClient		*gudev_client;
	GHashTable		*changed_idle_ids;	/* sysfs:FuUdevBackendHelper */
	GPtrArray		*subsystems;
};

G_DEFINE_TYPE (FuUdevBackend, fu_udev_backend, FU_TYPE_BACKEND)

static void
fu_udev_backend_device_add (FuUdevBackend *self, GUdevDevice *udev_device)
{
	g_autoptr(FuUdevDevice) device = fu_udev_device_new (udev_device);

	/* success */
	fu_backend_device_added (FU_BACKEND (self), FU_DEVICE (device));
}

static void
fu_udev_backend_device_remove (FuUdevBackend *self, GUdevDevice *udev_device)
{
	FuDevice *device_tmp;

	/* find the device we enumerated */
	device_tmp = fu_backend_lookup_by_id (FU_BACKEND (self),
					      g_udev_device_get_sysfs_path (udev_device));
	if (device_tmp != NULL) {
		if (g_getenv ("FWUPD_PROBE_VERBOSE") != NULL) {
			g_debug ("UDEV %s removed",
				 g_udev_device_get_sysfs_path (udev_device));
		}
		fu_backend_device_removed (FU_BACKEND (self), device_tmp);
	}
}

typedef struct {
	FuUdevBackend	*self;
	FuDevice	*device;
	guint		 idle_id;
} FuUdevBackendHelper;

static void
fu_udev_backend_changed_helper_free (FuUdevBackendHelper *helper)
{
	if (helper->idle_id != 0)
		g_source_remove (helper->idle_id);
	g_object_unref (helper->self);
	g_object_unref (helper->device);
	g_free (helper);
}

static FuUdevBackendHelper *
fu_udev_backend_changed_helper_new (FuUdevBackend *self, FuDevice *device)
{
	FuUdevBackendHelper *helper = g_new0 (FuUdevBackendHelper, 1);
	helper->self = g_object_ref (self);
	helper->device = g_object_ref (device);
	return helper;
}

static gboolean
fu_udev_backend_device_changed_cb (gpointer user_data)
{
	FuUdevBackendHelper *helper = (FuUdevBackendHelper *) user_data;
	fu_backend_device_changed (FU_BACKEND (helper->self), helper->device);
	helper->idle_id = 0;
	g_hash_table_remove (helper->self->changed_idle_ids,
			     fu_udev_device_get_sysfs_path (FU_UDEV_DEVICE (helper->device)));
	return FALSE;
}

static void
fu_udev_backend_device_changed (FuUdevBackend *self, GUdevDevice *udev_device)
{
	const gchar *sysfs_path = g_udev_device_get_sysfs_path (udev_device);
	FuUdevBackendHelper *helper;
	FuDevice *device_tmp;

	/* not a device we enumerated */
	device_tmp = fu_backend_lookup_by_id (FU_BACKEND (self), sysfs_path);
	if (device_tmp == NULL)
		return;

	/* run all plugins, with per-device rate limiting */
	if (g_hash_table_remove (self->changed_idle_ids, sysfs_path)) {
		g_debug ("re-adding rate-limited timeout for %s", sysfs_path);
	} else {
		g_debug ("adding rate-limited timeout for %s", sysfs_path);
	}
	helper = fu_udev_backend_changed_helper_new (self, device_tmp);
	helper->idle_id = g_timeout_add (500, fu_udev_backend_device_changed_cb, helper);
	g_hash_table_insert (self->changed_idle_ids, g_strdup (sysfs_path), helper);
}

static void
fu_udev_backend_uevent_cb (GUdevClient *gudev_client,
			   const gchar *action,
			   GUdevDevice *udev_device,
			   FuUdevBackend *self)
{
	if (g_strcmp0 (action, "add") == 0) {
		fu_udev_backend_device_add (self, udev_device);
		return;
	}
	if (g_strcmp0 (action, "remove") == 0) {
		fu_udev_backend_device_remove (self, udev_device);
		return;
	}
	if (g_strcmp0 (action, "change") == 0) {
		fu_udev_backend_device_changed (self, udev_device);
		return;
	}
}

static gboolean
fu_udev_backend_coldplug (FuBackend *backend, GError **error)
{
	FuUdevBackend *self = FU_UDEV_BACKEND (backend);

	/* udev watches can only be set up in _init() so set up client now */
	if (self->subsystems->len > 0) {
		g_auto(GStrv) subsystems = g_new0 (gchar *, self->subsystems->len + 1);
		for (guint i = 0; i < self->subsystems->len; i++) {
			const gchar *subsystem = g_ptr_array_index (self->subsystems, i);
			subsystems[i] = g_strdup (subsystem);
		}
		self->gudev_client = g_udev_client_new ((const gchar * const *) subsystems);
		g_signal_connect (self->gudev_client, "uevent",
				  G_CALLBACK (fu_udev_backend_uevent_cb), self);
	}

	/* get all devices of class */
	for (guint i = 0; i < self->subsystems->len; i++) {
		const gchar *subsystem = g_ptr_array_index (self->subsystems, i);
		GList *devices = g_udev_client_query_by_subsystem (self->gudev_client,
								   subsystem);
		if (g_getenv ("FWUPD_PROBE_VERBOSE") != NULL) {
			g_debug ("%u devices with subsystem %s",
				 g_list_length (devices), subsystem);
		}
		for (GList *l = devices; l != NULL; l = l->next) {
			GUdevDevice *udev_device = l->data;
			fu_udev_backend_device_add (self, udev_device);
		}
		g_list_foreach (devices, (GFunc) g_object_unref, NULL);
		g_list_free (devices);
	}

	return TRUE;
}

static void
fu_udev_backend_finalize (GObject *object)
{
	FuUdevBackend *self = FU_UDEV_BACKEND (object);
	if (self->gudev_client != NULL)
		g_object_unref (self->gudev_client);
	if (self->subsystems != NULL)
		g_ptr_array_unref (self->subsystems);
	g_hash_table_unref (self->changed_idle_ids);
	G_OBJECT_CLASS (fu_udev_backend_parent_class)->finalize (object);
}

static void
fu_udev_backend_init (FuUdevBackend *self)
{
	self->changed_idle_ids = g_hash_table_new_full (g_str_hash, g_str_equal,
							g_free, (GDestroyNotify) fu_udev_backend_changed_helper_free);
}

static void
fu_udev_backend_class_init (FuUdevBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuBackendClass *klass_backend = FU_BACKEND_CLASS (klass);
	object_class->finalize = fu_udev_backend_finalize;
	klass_backend->coldplug = fu_udev_backend_coldplug;
	klass_backend->recoldplug = fu_udev_backend_coldplug;
}

FuBackend *
fu_udev_backend_new (GPtrArray	*subsystems)
{
	FuUdevBackend *self;
	self = FU_UDEV_BACKEND (g_object_new (FU_TYPE_UDEV_BACKEND, "name", "udev", NULL));
	if (subsystems != NULL)
		self->subsystems = g_ptr_array_ref (subsystems);
	return FU_BACKEND (self);
}
