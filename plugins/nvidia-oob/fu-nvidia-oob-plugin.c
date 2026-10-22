/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-nvidia-oob-bmc.h"
#include "fu-nvidia-oob-device.h"
#include "fu-nvidia-oob-plugin.h"

struct _FuNvidiaOobPlugin {
	FuPlugin parent_instance;
	FuNvidiaOobBmc *bmc;
};

G_DEFINE_TYPE(FuNvidiaOobPlugin, fu_nvidia_oob_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_nvidia_oob_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuNvidiaOobPlugin *self = FU_NVIDIA_OOB_PLUGIN(plugin);
	const gchar *uri;
	const gchar *username;
	const gchar *password;

	/* the system is matched by its hardware ID, as the BMC can report an OEM brand */
	if (!fu_context_has_hwid_flag(fu_plugin_get_context(plugin), "nvidia-oob")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not an NVIDIA OOB platform");
		return FALSE;
	}

	/* the same BMC must not also be driven by the generic redfish plugin */
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "redfish");

	uri = fu_plugin_get_config_value(plugin, "Uri");
	if (uri == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no Uri set for the BMC");
		return FALSE;
	}
	if (!fu_nvidia_oob_bmc_set_uri(self->bmc, uri, error))
		return FALSE;
	fu_nvidia_oob_bmc_set_cacheck(self->bmc,
				      fu_plugin_get_config_value_boolean(plugin, "CACheck"));
	fu_nvidia_oob_bmc_set_session_key_file(
	    self->bmc,
	    fu_plugin_get_config_value(plugin, "SessionKeyFile"));
	username = fu_plugin_get_config_value(plugin, "Username");
	password = fu_plugin_get_config_value(plugin, "Password");
	fu_nvidia_oob_bmc_set_credentials(self->bmc, username, password);
	return TRUE;
}

static gboolean
fu_nvidia_oob_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuNvidiaOobPlugin *self = FU_NVIDIA_OOB_PLUGIN(plugin);
	g_autofree gchar *inventory_uri = NULL;
	g_autoptr(FuNvidiaOobRequest) request = NULL;
	g_autoptr(FwupdJsonArray) json_members = NULL;
	g_autoptr(FwupdJsonObject) json_inventory = NULL;

	if (!fu_nvidia_oob_bmc_setup(self->bmc, &inventory_uri, error))
		return FALSE;
	request = fu_nvidia_oob_bmc_request_new(self->bmc);
	if (!fu_nvidia_oob_request_perform(request, inventory_uri, error))
		return FALSE;
	json_inventory = fu_nvidia_oob_request_get_json_object(request, error);
	if (json_inventory == NULL)
		return FALSE;
	json_members = fwupd_json_object_get_array(json_inventory, "Members", error);
	if (json_members == NULL)
		return FALSE;
	for (guint i = 0; i < fwupd_json_array_get_size(json_members); i++) {
		const gchar *member_uri;
		g_autoptr(FuDeviceLocker) locker = NULL;
		g_autoptr(FuNvidiaOobDevice) device = NULL;
		g_autoptr(FuNvidiaOobRequest) request_member = NULL;
		g_autoptr(FwupdJsonObject) json_member = NULL;
		g_autoptr(FwupdJsonObject) json_ref = NULL;
		g_autoptr(GError) error_local = NULL;

		json_ref = fwupd_json_array_get_object(json_members, i, error);
		if (json_ref == NULL)
			return FALSE;
		member_uri = fwupd_json_object_get_string(json_ref, "@odata.id", error);
		if (member_uri == NULL)
			return FALSE;
		request_member = fu_nvidia_oob_bmc_request_new(self->bmc);
		if (!fu_nvidia_oob_request_perform(request_member, member_uri, error))
			return FALSE;
		json_member = fu_nvidia_oob_request_get_json_object(request_member, error);
		if (json_member == NULL)
			return FALSE;

		device =
		    fu_nvidia_oob_device_new(fu_plugin_get_context(plugin), self->bmc, json_member);
		locker = fu_device_locker_new(FU_DEVICE(device), &error_local);
		if (locker == NULL) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
				g_debug("ignoring %s: %s", member_uri, error_local->message);
				continue;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		fu_nvidia_oob_bmc_add_device(self->bmc, FU_DEVICE(device));
		fu_plugin_add_device(plugin, FU_DEVICE(device));
	}
	return TRUE;
}

static gboolean
fu_nvidia_oob_plugin_composite_prepare(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuNvidiaOobPlugin *self = FU_NVIDIA_OOB_PLUGIN(plugin);
	fu_nvidia_oob_bmc_set_composite(self->bmc, TRUE);
	return TRUE;
}

static gboolean
fu_nvidia_oob_plugin_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuNvidiaOobPlugin *self = FU_NVIDIA_OOB_PLUGIN(plugin);
	FuDevice *device_written = fu_nvidia_oob_bmc_get_written_device(self->bmc);
	g_autoptr(FuDevice) device = NULL;

	/* every component of the archive has now been processed, so flagging the ones
	 * the BMC staged can no longer block the install of another */
	if (device_written != NULL)
		device = g_object_ref(device_written);
	fu_nvidia_oob_bmc_set_written_device(self->bmc, NULL);
	fu_nvidia_oob_bmc_set_composite(self->bmc, FALSE);
	if (device == NULL)
		return TRUE;
	return fu_nvidia_oob_device_refresh_pending(FU_NVIDIA_OOB_DEVICE(device), error);
}

static void
fu_nvidia_oob_plugin_init(FuNvidiaOobPlugin *self)
{
	self->bmc = fu_nvidia_oob_bmc_new();
}

static void
fu_nvidia_oob_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);

	/* startup has to disable the redfish plugin before it talks to the BMC */
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_RUN_BEFORE, "redfish");
	fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_SECURE_CONFIG);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_NVIDIA_OOB_DEVICE); /* coverage */

	/* defaults changed here will also be reflected in the fwupd.conf man page */
	fu_plugin_set_config_default(plugin, "CACheck", "false");
	fu_plugin_set_config_default(plugin, "Password", NULL);
	fu_plugin_set_config_default(plugin, "SessionKeyFile", NULL);
	fu_plugin_set_config_default(plugin, "Uri", NULL);
	fu_plugin_set_config_default(plugin, "Username", NULL);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_nvidia_oob_plugin_parent_class)->constructed(obj);
}

static void
fu_nvidia_oob_plugin_finalize(GObject *obj)
{
	FuNvidiaOobPlugin *self = FU_NVIDIA_OOB_PLUGIN(obj);

	/* the devices hold the BMC too */
	fu_nvidia_oob_bmc_remove_devices(self->bmc);
	g_object_unref(self->bmc);
	G_OBJECT_CLASS(fu_nvidia_oob_plugin_parent_class)->finalize(obj);
}

static void
fu_nvidia_oob_plugin_class_init(FuNvidiaOobPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_nvidia_oob_plugin_finalize;
	plugin_class->constructed = fu_nvidia_oob_plugin_constructed;
	plugin_class->startup = fu_nvidia_oob_plugin_startup;
	plugin_class->coldplug = fu_nvidia_oob_plugin_coldplug;
	plugin_class->composite_prepare = fu_nvidia_oob_plugin_composite_prepare;
	plugin_class->composite_cleanup = fu_nvidia_oob_plugin_composite_cleanup;
}
