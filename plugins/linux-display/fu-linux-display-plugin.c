/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-linux-display-plugin.h"

struct _FuLinuxDisplayPlugin {
	FuPlugin parent_instance;
	GPtrArray *devices;
};

G_DEFINE_TYPE(FuLinuxDisplayPlugin, fu_linux_display_plugin, FU_TYPE_PLUGIN)

static FuDisplayState
fu_linux_display_plugin_get_display_state(FuLinuxDisplayPlugin *self)
{
	FuDisplayState display_state = FU_DISPLAY_STATE_DISCONNECTED;

	/* no devices detected */
	if (self->devices->len == 0)
		return FU_DISPLAY_STATE_UNKNOWN;

	/* any connected display is good enough */
	for (guint i = 0; i < self->devices->len; i++) {
		FuDevice *device = g_ptr_array_index(self->devices, i);
		const gchar *status =
		    fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(device), "status", NULL);
		if (g_strcmp0(status, "connected") == 0) {
			display_state = FU_DISPLAY_STATE_CONNECTED;
			break;
		}
	}
	return display_state;
}

static void
fu_linux_display_plugin_ensure_display_state(FuLinuxDisplayPlugin *self)
{
	FuContext *ctx = fu_plugin_get_context(FU_PLUGIN(self));
	if (!fu_plugin_has_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_READY))
		return;
	fu_context_set_display_state(ctx, fu_linux_display_plugin_get_display_state(self));
}

static gboolean
fu_linux_display_plugin_plugin_backend_device_added(FuPlugin *plugin,
						    FuDevice *device,
						    FuProgress *progress,
						    GError **error)
{
	FuLinuxDisplayPlugin *self = FU_LINUX_DISPLAY_PLUGIN(plugin);
	g_ptr_array_add(self->devices, g_object_ref(device));
	fu_linux_display_plugin_ensure_display_state(self);
	return TRUE;
}

static gboolean
fu_linux_display_plugin_plugin_ready(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuLinuxDisplayPlugin *self = FU_LINUX_DISPLAY_PLUGIN(plugin);
	fu_linux_display_plugin_ensure_display_state(self);
	return TRUE;
}

static gboolean
fu_linux_display_plugin_plugin_backend_device_removed(FuPlugin *plugin,
						      FuDevice *device,
						      GError **error)
{
	FuLinuxDisplayPlugin *self = FU_LINUX_DISPLAY_PLUGIN(plugin);
	g_ptr_array_remove(self->devices, device);
	fu_linux_display_plugin_ensure_display_state(self);
	return TRUE;
}

static gboolean
fu_linux_display_plugin_plugin_backend_device_changed(FuPlugin *plugin,
						      FuDevice *device,
						      GError **error)
{
	FuLinuxDisplayPlugin *self = FU_LINUX_DISPLAY_PLUGIN(plugin);
	if (!FU_IS_UDEV_DEVICE(device))
		return TRUE;
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "drm") != 0)
		return TRUE;
	fu_linux_display_plugin_ensure_display_state(self);
	return TRUE;
}

static void
fu_linux_display_plugin_init(FuLinuxDisplayPlugin *self)
{
	self->devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static void
fu_linux_display_plugin_finalize(GObject *obj)
{
	FuLinuxDisplayPlugin *self = FU_LINUX_DISPLAY_PLUGIN(obj);
	g_ptr_array_unref(self->devices);
	G_OBJECT_CLASS(fu_linux_display_plugin_parent_class)->finalize(obj);
}

static void
fu_ata_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_udev_subsystem(plugin, "drm");
}

static void
fu_linux_display_plugin_class_init(FuLinuxDisplayPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	object_class->finalize = fu_linux_display_plugin_finalize;
	plugin_class->constructed = fu_ata_plugin_constructed;
	plugin_class->ready = fu_linux_display_plugin_plugin_ready;
	plugin_class->backend_device_added = fu_linux_display_plugin_plugin_backend_device_added;
	plugin_class->backend_device_removed =
	    fu_linux_display_plugin_plugin_backend_device_removed;
	plugin_class->backend_device_changed =
	    fu_linux_display_plugin_plugin_backend_device_changed;
}
