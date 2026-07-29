/*
 * Copyright 2026 NVIDIA Corporation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * plugin class for the Galaxy GB300 LVFS OOB firmware update path
 *
 * responsibilities:
 *   - startup():  curl_global_init + build the shared Redfish client
 *   - coldplug(): list FirmwareInventory members and wrap each one
 *                 in a FuNvidiaOobDevice
 *
 * the actual update work (prepare_firmware, write_firmware, attach)
 * lives on the FuDevice subclass in fu-nvidia-oob-device.c
 */

#include "config.h"

#include <curl/curl.h>

#include "fu-nvidia-oob-device.h"
#include "fu-nvidia-oob-plugin.h"
#include "fu-nvidia-oob-redfish-client.h"

static void
fu_nvidia_oob_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	(void)fu_plugin_alloc_data(plugin, sizeof(FuNvidiaOobPlugin));
	/* plugin name is derived from the .so filename (libfu_plugin_nvidia_oob_redfish.so)
	 * in fwupd >= 2.0; fu_plugin_set_name() was removed in that release */
	/* conflict with the upstream generic redfish plugin so we own
	 * the GB300 BMC inventory exclusively */
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "redfish");
	/* register the device GType so quirk-file stanzas matched on
	 * instance IDs are dispatched against FuNvidiaOobDevice */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_NVIDIA_OOB_DEVICE);
}

static gboolean
fu_nvidia_oob_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuNvidiaOobPlugin *self = FU_NVIDIA_OOB_PLUGIN(plugin);

	g_debug("%s: plugin v%s loaded", NVIDIA_OOB_PLUGIN_NAME, NVIDIA_OOB_PLUGIN_VERSION);

	if (!self->curl_global_inited) {
		if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "curl_global_init failed");
			return FALSE;
		}
		self->curl_global_inited = TRUE;
	}

	self->client = fu_nvidia_oob_redfish_client_new();
	if (!fu_nvidia_oob_redfish_client_setup(self->client, error)) {
		g_prefix_error(error, "OOB Redfish client setup failed: ");
		g_clear_object(&self->client);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_nvidia_oob_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuNvidiaOobPlugin *self = FU_NVIDIA_OOB_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);

	g_autoptr(GPtrArray) uris =
	    fu_nvidia_oob_redfish_client_list_inventory(self->client, error);
	if (uris == NULL)
		return FALSE;
	if (uris->len == 0) {
		g_debug("BMC FirmwareInventory is empty -- no OOB devices to register");
		return TRUE;
	}

	for (guint i = 0; i < uris->len; i++) {
		const gchar *uri = g_ptr_array_index(uris, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FwupdJsonNode) node =
		    fu_nvidia_oob_redfish_client_get(self->client, uri, &error_local);
		g_autoptr(FwupdJsonObject) obj = NULL;
		g_autoptr(FuNvidiaOobDevice) device = NULL;
		gboolean updateable = TRUE;
		if (node == NULL) {
			g_warning("failed to GET %s: %s", uri, error_local->message);
			continue;
		}
		obj = fwupd_json_node_get_object(node, &error_local);
		if (obj == NULL) {
			g_warning("failed to parse %s: %s", uri, error_local->message);
			continue;
		}

		/* only register components the BMC reports as updatable -- the Redfish
		 * SoftwareInventory 'Updateable' field is false for read-only entries
		 * that cannot be flashed out-of-band; registering them would expose
		 * devices fwupd can never update; default to TRUE when the field is
		 * absent so BMCs that omit it keep working */
		if (!fwupd_json_object_get_boolean_with_default(obj,
								"Updateable",
								&updateable,
								TRUE,
								&error_local)) {
			g_warning("nvidia-oob: failed to read Updateable for %s: %s",
				  uri,
				  error_local->message);
			continue;
		}
		if (!updateable) {
			g_debug("nvidia-oob: skipping non-updateable inventory member %s", uri);
			continue;
		}

		device = fu_nvidia_oob_device_new(ctx, self->client, uri, obj);
		fu_plugin_add_device(plugin, FU_DEVICE(device));
	}
	return TRUE;
}

static void
fu_nvidia_oob_plugin_finalize(GObject *obj)
{
	FuNvidiaOobPlugin *self = FU_NVIDIA_OOB_PLUGIN(obj);
	g_clear_object(&self->client);
	if (self->curl_global_inited)
		curl_global_cleanup();
}

/* fwupd plugin registration entry point */

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->constructed = fu_nvidia_oob_plugin_constructed;
	vfuncs->finalize = fu_nvidia_oob_plugin_finalize;
	vfuncs->startup = fu_nvidia_oob_plugin_startup;
	vfuncs->coldplug = fu_nvidia_oob_plugin_coldplug;
}
