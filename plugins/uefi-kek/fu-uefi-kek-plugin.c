/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-kek-device.h"
#include "fu-uefi-kek-plugin.h"

struct _FuUefiKekPlugin {
	FuPlugin parent_instance;
	FuDevice *device_pk;
};

G_DEFINE_TYPE(FuUefiKekPlugin, fu_uefi_kek_plugin, FU_TYPE_PLUGIN)

static void
fu_uefi_kek_plugin_init(FuUefiKekPlugin *self)
{
}

static void
fu_uefi_kek_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "uefi_pk");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UEFI_KEK_DEVICE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_uefi_kek_plugin_parent_class)->constructed(obj);
}

static void
fu_uefi_kek_plugin_ensure_external_locked(FuUefiKekPlugin *self, FuDevice *device_kek)
{
	if (self->device_pk == NULL)
		return;
	if (fu_device_has_private_flag(self->device_pk, FU_UEFI_DEVICE_PRIVATE_FLAG_IS_EXTERNAL)) {
		GPtrArray *device_children = fu_device_get_children(device_kek);
		for (guint i = 0; i < device_children->len; i++) {
			FuDevice *device_tmp = g_ptr_array_index(device_children, i);
			fu_device_add_problem(device_tmp, FWUPD_DEVICE_PROBLEM_FIRMWARE_LOCKED);
		}
	}
}

static void
fu_uefi_kek_plugin_device_added(FuPlugin *plugin, FuDevice *device)
{
	FuUefiKekPlugin *self = FU_UEFI_KEK_PLUGIN(plugin);
	fu_uefi_kek_plugin_ensure_external_locked(self, device);
}

static void
fu_uefi_kek_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	FuUefiKekPlugin *self = FU_UEFI_KEK_PLUGIN(plugin);
	if (g_strcmp0(fu_device_get_plugin(device), "uefi_pk") == 0) {
		GPtrArray *devices = fu_plugin_get_devices(plugin);
		g_set_object(&self->device_pk, device);
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device_tmp = g_ptr_array_index(devices, i);
			fu_uefi_kek_plugin_ensure_external_locked(self, device_tmp);
		}
	}
}

static void
fu_uefi_kek_plugin_finalize(GObject *obj)
{
	FuUefiKekPlugin *self = FU_UEFI_KEK_PLUGIN(obj);
	g_clear_object(&self->device_pk);
	G_OBJECT_CLASS(fu_uefi_kek_plugin_parent_class)->finalize(obj);
}

static void
fu_uefi_kek_plugin_class_init(FuUefiKekPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_uefi_kek_plugin_finalize;
	plugin_class->constructed = fu_uefi_kek_plugin_constructed;
	plugin_class->device_added = fu_uefi_kek_plugin_device_added;
	plugin_class->device_registered = fu_uefi_kek_plugin_device_registered;
}
