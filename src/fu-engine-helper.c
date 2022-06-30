/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuEngine"

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gi18n.h>

#include "fwupd-device-private.h"

#include "fu-engine-helper.h"
#include "fu-engine.h"

static FwupdRelease *
fu_engine_get_release_with_tag(FuEngine *self,
			       FuEngineRequest *request,
			       FwupdDevice *dev,
			       const gchar *tag,
			       GError **error)
{
	g_autoptr(GPtrArray) rels = NULL;

	/* find the newest release that matches */
	rels = fu_engine_get_releases(self, request, fwupd_device_get_id(dev), error);
	if (rels == NULL)
		return NULL;
	for (guint i = 0; i < rels->len; i++) {
		FwupdRelease *rel = g_ptr_array_index(rels, i);
		if (fwupd_release_has_tag(rel, tag))
			return g_object_ref(rel);
	}

	/* no match */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no matching releases for device");
	return NULL;
}

gboolean
fu_engine_update_motd(FuEngine *self, GError **error)
{
	const gchar *host_bkc = fu_engine_get_host_bkc(self);
	guint upgrade_count = 0;
	guint sync_count = 0;
	g_autoptr(FuEngineRequest) request = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GString) str = g_string_new(NULL);
	g_autofree gchar *target = NULL;

	/* a subset of what fwupdmgr can do */
	request = fu_engine_request_new(FU_ENGINE_REQUEST_KIND_ACTIVE);
	fu_engine_request_set_feature_flags(request,
					    FWUPD_FEATURE_FLAG_DETACH_ACTION |
						FWUPD_FEATURE_FLAG_UPDATE_ACTION);

	/* get devices from daemon, we even want to know if it's nothing */
	devices = fu_engine_get_devices(self, NULL);
	if (devices != NULL) {
		for (guint i = 0; i < devices->len; i++) {
			FwupdDevice *dev = g_ptr_array_index(devices, i);
			g_autoptr(GPtrArray) rels = NULL;

			/* get the releases for this device */
			rels =
			    fu_engine_get_upgrades(self, request, fwupd_device_get_id(dev), NULL);
			if (rels == NULL)
				continue;
			upgrade_count++;
		}
		if (host_bkc != NULL) {
			for (guint i = 0; i < devices->len; i++) {
				FwupdDevice *dev = g_ptr_array_index(devices, i);
				g_autoptr(FwupdRelease) rel = NULL;
				rel = fu_engine_get_release_with_tag(self,
								     request,
								     dev,
								     host_bkc,
								     NULL);
				if (rel == NULL)
					continue;
				if (g_strcmp0(fwupd_device_get_version(dev),
					      fwupd_release_get_version(rel)) != 0)
					sync_count++;
			}
		}
	}

	/* If running under systemd unit, use the directory as a base */
	if (g_getenv("RUNTIME_DIRECTORY") != NULL) {
		target = g_build_filename(g_getenv("RUNTIME_DIRECTORY"), MOTD_FILE, NULL);
		/* otherwise use the cache directory */
	} else {
		g_autofree gchar *directory = fu_path_from_kind(FU_PATH_KIND_CACHEDIR_PKG);
		target = g_build_filename(directory, MOTD_DIR, MOTD_FILE, NULL);
	}

	/* create the directory and file, even if zero devices; we want an empty file then */
	if (!fu_path_mkdir_parent(target, error))
		return FALSE;

	/* nag about syncing or updating, but never both */
	if (sync_count > 0) {
		g_string_append(str, "\n");
		g_string_append_printf(str,
				       /* TRANSLATORS: this is shown in the MOTD */
				       ngettext("%u device is not the best known configuration.",
						"%u devices are not the best known configuration.",
						sync_count),
				       sync_count);
		g_string_append_printf(str,
				       "\n%s\n\n",
				       /* TRANSLATORS: this is shown in the MOTD */
				       _("Run `fwupdmgr sync-bkc` to complete this action."));
	} else if (upgrade_count > 0) {
		g_string_append(str, "\n");
		g_string_append_printf(str,
				       /* TRANSLATORS: this is shown in the MOTD */
				       ngettext("%u device has a firmware upgrade available.",
						"%u devices have a firmware upgrade available.",
						upgrade_count),
				       upgrade_count);
		g_string_append_printf(str,
				       "\n%s\n\n",
				       /* TRANSLATORS: this is shown in the MOTD */
				       _("Run `fwupdmgr get-upgrades` for more information."));
	}

	/* success, with an empty file if nothing to say */
	g_debug("writing motd target %s", target);
	return g_file_set_contents(target, str->str, str->len, error);
}

gboolean
fu_engine_update_devices_file(FuEngine *self, GError **error)
{
	FwupdDeviceFlags flags = FWUPD_DEVICE_FLAG_NONE;
	gsize len;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) generator = NULL;
	g_autoptr(JsonNode) root = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autofree gchar *data = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar *target = NULL;

	if (fu_config_get_show_device_private(fu_engine_get_config(self)))
		flags |= FWUPD_DEVICE_FLAG_TRUSTED;

	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "Devices");
	json_builder_begin_array(builder);
	devices = fu_engine_get_devices(self, NULL);
	if (devices != NULL) {
		for (guint i = 0; i < devices->len; i++) {
			FwupdDevice *dev = g_ptr_array_index(devices, i);
			json_builder_begin_object(builder);
			fwupd_device_to_json_full(dev, builder, flags);
			json_builder_end_object(builder);
		}
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);

	root = json_builder_get_root(builder);
	generator = json_generator_new();
	json_generator_set_pretty(generator, TRUE);
	json_generator_set_root(generator, root);
	data = json_generator_to_data(generator, &len);
	if (data == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Failed to convert to JSON string");
		return FALSE;
	}

	directory = fu_path_from_kind(FU_PATH_KIND_CACHEDIR_PKG);
	target = g_build_filename(directory, "devices.json", NULL);
	return g_file_set_contents(target, data, (gssize)len, error);
}
