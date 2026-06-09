/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-db-device.h"
#include "fu-uefi-db-plugin.h"

struct _FuUefiDbPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuUefiDbPlugin, fu_uefi_db_plugin, FU_TYPE_PLUGIN)

static void
fu_uefi_db_plugin_init(FuUefiDbPlugin *self)
{
}

static void
fu_uefi_db_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "uefi_pk");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UEFI_DB_DEVICE);

	/* defaults changed here will also be reflected in the fwupd.conf man page */
	fu_plugin_set_config_default(plugin, "UpdateWindowsCA", "false");

	/* chain up to parent */
	G_OBJECT_CLASS(fu_uefi_db_plugin_parent_class)->constructed(obj);
}

static gboolean
fu_uefi_db_plugin_modify_config(FuPlugin *plugin,
				const gchar *key,
				const gchar *value,
				GError **error)
{
	const gchar *keys[] = {"UpdateWindowsCA", NULL};
	if (!g_strv_contains(keys, key)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "config key %s not supported",
			    key);
		return FALSE;
	}
	return fu_plugin_set_config_value(plugin, key, value, error);
}

static void
fu_uefi_db_plugin_device_child_added(FuPlugin *plugin, FuDevice *device)
{
	if (fu_device_has_instance_id(device,
				      /* Windows Production PCA */
				      "UEFI\\CRT_1A8B6903D64CC9AD09D12FCB355663A458A09EF0",
				      FU_DEVICE_INSTANCE_FLAG_VISIBLE))
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY);
}

static void
fu_uefi_db_plugin_device_added(FuPlugin *plugin, FuDevice *device)
{
	FuContext *ctx = fu_plugin_get_context(plugin);

	if (!fu_plugin_get_config_value_boolean(plugin, "UpdateWindowsCA") &&
	    !fu_context_has_flag(ctx, FU_CONTEXT_FLAG_DUAL_BOOT_WINDOWS)) {
		GPtrArray *devices = fu_device_get_children(device);
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device_child = g_ptr_array_index(devices, i);
			fu_uefi_db_plugin_device_child_added(plugin, device_child);
		}
	}
}

static void
fu_uefi_db_plugin_class_init(FuUefiDbPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_uefi_db_plugin_constructed;
	plugin_class->modify_config = fu_uefi_db_plugin_modify_config;
	plugin_class->device_added = fu_uefi_db_plugin_device_added;
}
