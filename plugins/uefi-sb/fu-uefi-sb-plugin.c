/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-sb-db-device.h"
#include "fu-uefi-sb-dbx-device.h"
#include "fu-uefi-sb-kek-device.h"
#include "fu-uefi-sb-pk-device.h"
#include "fu-uefi-sb-plugin.h"

struct _FuUefiSbPlugin {
	FuPlugin parent_instance;
	FuDevice *device_pk;
	FuDevice *device_kek;
	FuDevice *device_db;
};

G_DEFINE_TYPE(FuUefiSbPlugin, fu_uefi_sb_plugin, FU_TYPE_PLUGIN)

static void
fu_uefi_sb_plugin_ensure_external_locked(FuUefiSbPlugin *self, FuDevice *device_kek)
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
fu_uefi_sb_plugin_device_added(FuPlugin *plugin, FuDevice *device)
{
	FuUefiSbPlugin *self = FU_UEFI_SB_PLUGIN(plugin);
	fu_uefi_sb_plugin_ensure_external_locked(self, device);
#if 0
	DB
	if (fu_device_has_instance_id(device,
				      /* Windows Production PCA */
				      "UEFI\\CRT_1A8B6903D64CC9AD09D12FCB355663A458A09EF0",
				      FU_DEVICE_INSTANCE_FLAG_VISIBLE))
		fu_device_add_problem(device, FWUPD_DEVICE_PROBLEM_LOWER_PRIORITY);

	if (!fu_plugin_get_config_value_boolean(plugin, "UpdateWindowsCA") &&
	    !fu_context_has_flag(ctx, FU_CONTEXT_FLAG_DUAL_BOOT_WINDOWS)) {
		GPtrArray *devices = fu_device_get_children(device);
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device_child = g_ptr_array_index(devices, i);
			fu_uefi_sb_plugin_device_child_added(plugin, device_child);
		}
	}

	DBX
	FuContext *ctx = fu_plugin_get_context(plugin);
	if (fu_context_has_hwid_flag(ctx, "no-dbx-updates")) {
		fu_device_inhibit(FU_DEVICE(device),
				  "no-dbx",
				  "System firmware cannot accept DBX updates");
	}


	FuContext *ctx = fu_backend_get_context(backend);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	FuEfiSecureBootSiglistKind kinds[] = {
	    FU_EFI_SECURE_BOOT_SIGLIST_KIND_PK,
	    FU_EFI_SECURE_BOOT_SIGLIST_KIND_KEK,
	    FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB,
	    FU_EFI_SECURE_BOOT_SIGLIST_KIND_DBX,
	};

	/* each important EFI key */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, G_N_ELEMENTS(kinds));
	for (guint i = 0; i < G_N_ELEMENTS(kinds); i++) {
		g_autoptr(FuUefiDevice) uefi_device = NULL;
		if (!fu_efivars_exists(efivars,
				       fu_efivars_secure_boot_siglist_kind_to_guid(kinds[i]),
				       fu_efivars_secure_boot_siglist_kind_to_name(kinds[i]))) {
			fu_progress_step_done(progress);
			continue;
		}
		uefi_device = fu_uefi_device_new(ctx, kinds[i]);
		fu_backend_device_added(backend, FU_DEVICE(uefi_device));
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;

#endif
}

static gboolean
fu_uefi_sb_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuUefiSbPlugin *self = FU_UEFI_SB_PLUGIN(plugin);

	if (!fu_device_probe(self->device_pk, error))
		return FALSE;
	fu_plugin_add_report_metadata(
	    plugin,
	    "UefiPkKeyId",
	    fu_uefi_sb_pk_device_get_key_id(FU_UEFI_PK_DEVICE(self->device_pk)));

	if (self->device_kek == NULL)
		return FALSE;
	if (fu_device_has_private_flag(self->device_kek, FU_UEFI_DEVICE_PRIVATE_FLAG_IS_EXTERNAL)) {
		GPtrArray *device_children = fu_device_get_children(self->device_db);
		for (guint i = 0; i < device_children->len; i++) {
			FuDevice *device_tmp = g_ptr_array_index(device_children, i);
			fu_device_add_problem(device_tmp, FWUPD_DEVICE_PROBLEM_FIRMWARE_LOCKED);
		}
	}
	return TRUE;
}

static void
fu_uefi_sb_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	FuUefiSbPlugin *self = FU_UEFI_SB_PLUGIN(plugin);
	if (g_strcmp0(fu_device_get_plugin(device), "uefi_pk") == 0) {
		GPtrArray *devices = fu_plugin_get_devices(plugin);
		g_set_object(&self->device_pk, device);
		for (guint i = 0; i < devices->len; i++) {
			FuDevice *device_tmp = g_ptr_array_index(devices, i);
			fu_uefi_sb_plugin_ensure_external_locked(self, device_tmp);
		}
	}
}

static gboolean
fu_uefi_sb_plugin_modify_config(FuPlugin *plugin,
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
fu_uefi_sb_plugin_init(FuUefiSbPlugin *self)
{
}

static void
fu_uefi_sb_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	g_autoptr(FuVolume) esp = NULL;
	g_autoptr(GError) error_udisks2 = NULL;
	FuContext *ctx = fu_plugin_get_context(plugin);

	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "uefi_pk");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UEFI_KEK_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UEFI_PK_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UEFI_DB_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UEFI_DBX_DEVICE);

	/* defaults changed here will also be reflected in the fwupd.conf man page */
	fu_plugin_set_config_default(plugin, "UpdateWindowsCA", "false");
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_EFI_SIGNATURE_LIST);

	/* ensure that an ESP was found */
	esp = fu_context_get_default_esp(ctx, &error_udisks2);
	if (esp == NULL) {
		g_info("cannot find default ESP: %s", error_udisks2->message);
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND);
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_USER_WARNING);
	}

	/* chain up to parent */
	G_OBJECT_CLASS(fu_uefi_sb_plugin_parent_class)->constructed(obj);
}

static void
fu_uefi_sb_plugin_finalize(GObject *obj)
{
	FuUefiSbPlugin *self = FU_UEFI_SB_PLUGIN(obj);
	g_clear_object(&self->device_pk);
	g_clear_object(&self->device_kek);
	g_clear_object(&self->device_db);
	G_OBJECT_CLASS(fu_uefi_sb_plugin_parent_class)->finalize(obj);
}

static void
fu_uefi_sb_plugin_class_init(FuUefiSbPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_uefi_sb_plugin_finalize;
	plugin_class->modify_config = fu_uefi_sb_plugin_modify_config;
	plugin_class->constructed = fu_uefi_sb_plugin_constructed;
	plugin_class->coldplug = fu_uefi_sb_plugin_coldplug;
	plugin_class->device_added = fu_uefi_sb_plugin_device_added;
	plugin_class->device_registered = fu_uefi_sb_plugin_device_registered;
}
