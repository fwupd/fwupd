/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-dbx-device.h"
#include "fu-uefi-dbx-plugin.h"
#include "fu-uefi-dbx-snapd-notifier.h"

struct _FuUefiDbxPlugin {
	FuPlugin parent_instance;

	FuUefiDbxSnapdNotifier *snapd_notifier;
	gboolean snapd_integration_supported;
};

G_DEFINE_TYPE(FuUefiDbxPlugin, fu_uefi_dbx_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_uefi_dbx_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuUefiDbxDevice) device = fu_uefi_dbx_device_new(ctx);
	FuUefiDbxPlugin *self = FU_UEFI_DBX_PLUGIN(plugin);
	gboolean inhibited = FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 99, "probe");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "setup");

	if (!fu_device_probe(FU_DEVICE(device), error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_device_setup(FU_DEVICE(device), error))
		return FALSE;
	fu_progress_step_done(progress);

	if (fu_context_has_hwid_flag(fu_plugin_get_context(plugin), "no-dbx-updates")) {
		fu_device_inhibit(FU_DEVICE(device),
				  "no-dbx",
				  "System firmware cannot accept DBX updates");
		inhibited = TRUE;
	}

	if (self->snapd_notifier != NULL) {
		fu_uefi_dbx_device_set_snapd_notifier(device, self->snapd_notifier);
	} else if (!inhibited && self->snapd_integration_supported && fu_snap_is_in_snap()) {
		/* we're running inside a snap, the device is not inhibited and snapd
		 * supports integration, in which case this is a hard error and we
		 * should not give an option to dbx */

		/* TODO should check for FDE flag, otherwise in a system where DBX is
		measured during the boot, updating it without notifying snapd
		can result in failed boot or needing to use recovery keys */
		fu_device_inhibit(FU_DEVICE(device),
				  "no-snapd-dbx",
				  "Snapd integration for DBX update is not available");
	}

	fu_plugin_device_add(plugin, FU_DEVICE(device));
	return TRUE;
}

static void
fu_uefi_dbx_plugin_init(FuUefiDbxPlugin *self)
{
}

static void
fu_uefi_dbx_plugin_finalize(GObject *object)
{
	FuUefiDbxPlugin *self = FU_UEFI_DBX_PLUGIN(object);
	if (self->snapd_notifier != NULL) {
		g_object_unref(self->snapd_notifier);
		self->snapd_notifier = NULL;
	}

	G_OBJECT_CLASS(fu_uefi_dbx_plugin_parent_class)->finalize(object);
}

static gboolean
fu_uefi_dbx_plugin_snapd_notify_init(FuUefiDbxPlugin *self, GError **error)
{
	g_autoptr(FuUefiDbxSnapdNotifier) obs = fu_uefi_dbx_snapd_notifier_new();

	if (!fu_uefi_dbx_snapd_notifier_dbx_manager_startup(obs, error))
		return FALSE;

	g_set_object(&self->snapd_notifier, obs);
	return TRUE;
}

static void
fu_uefi_dbx_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "uefi_capsule");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_EFI_SIGNATURE_LIST);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UEFI_DBX_DEVICE);

	if (fu_snap_is_in_snap()) {
		FuUefiDbxPlugin *self = FU_UEFI_DBX_PLUGIN(plugin);
		g_autoptr(GError) error_local = NULL;
		/* only enable snapd integration if running inside a snap */
		if (!fu_uefi_dbx_plugin_snapd_notify_init(FU_UEFI_DBX_PLUGIN(obj), &error_local)) {
			/* specific error code if relevant APIs are not present and thus
			 * integration cannot be supported */
			self->snapd_integration_supported =
			    !g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);

			g_info("snapd integration non-functional: %s", error_local->message);
		} else {
			g_info("snapd integration enabled ");
			self->snapd_integration_supported = TRUE;
		}
	} else {
		/* TODO figure out non-snap scenarios */
		g_info("snapd integration outside of snap is not supported");
	}
}

static void
fu_uefi_dbx_plugin_class_init(FuUefiDbxPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);

	plugin_class->constructed = fu_uefi_dbx_plugin_constructed;
	plugin_class->coldplug = fu_uefi_dbx_plugin_coldplug;

	object_class->finalize = fu_uefi_dbx_plugin_finalize;
}
