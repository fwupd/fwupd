/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-linux-display-plugin.h"

struct _FuLinuxDisplayPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLinuxDisplayPlugin, fu_linux_display_plugin, FU_TYPE_PLUGIN)

static FuDisplayState
fu_linux_display_plugin_get_display_state(FuLinuxDisplayPlugin *self)
{
	FuDisplayState display_state = FU_DISPLAY_STATE_DISCONNECTED;
	GPtrArray *devices = fu_plugin_get_devices(FU_PLUGIN(self));

	/* no devices detected */
	if (devices->len == 0)
		return FU_DISPLAY_STATE_UNKNOWN;

	/* any connected display is good enough */
	for (guint i = 0; i < devices->len; i++) {
		FuDrmDevice *drm_device = g_ptr_array_index(devices, i);
		if (fu_drm_device_get_state(drm_device) == FU_DISPLAY_STATE_CONNECTED) {
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
	if (fu_drm_device_get_edid(FU_DRM_DEVICE(device)) != NULL) {
		if (!fu_device_setup(device, error))
			return FALSE;
		fu_plugin_device_add(plugin, device);
	}
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
	fu_linux_display_plugin_ensure_display_state(self);
	return TRUE;
}

static gboolean
fu_linux_display_plugin_plugin_backend_device_changed(FuPlugin *plugin,
						      FuDevice *device,
						      GError **error)
{
	FuLinuxDisplayPlugin *self = FU_LINUX_DISPLAY_PLUGIN(plugin);
	if (!FU_IS_DRM_DEVICE(device))
		return TRUE;
	fu_linux_display_plugin_ensure_display_state(self);
	return TRUE;
}

static void
fu_linux_display_plugin_init(FuLinuxDisplayPlugin *self)
{
}

static void
fu_linux_display_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_udev_subsystem(plugin, "drm");
}

static void
fu_linux_display_plugin_class_init(FuLinuxDisplayPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_linux_display_plugin_constructed;
	plugin_class->ready = fu_linux_display_plugin_plugin_ready;
	plugin_class->backend_device_added = fu_linux_display_plugin_plugin_backend_device_added;
	plugin_class->backend_device_removed =
	    fu_linux_display_plugin_plugin_backend_device_removed;
	plugin_class->backend_device_changed =
	    fu_linux_display_plugin_plugin_backend_device_changed;
}
