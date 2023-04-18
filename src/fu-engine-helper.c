/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
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
	request = fu_engine_request_new();
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
			if (!fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE))
				continue;
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
				if (!fu_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE))
					continue;
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

	if (fu_engine_config_get_show_device_private(fu_engine_get_config(self)))
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

static void
fu_engine_integrity_add_measurement(GHashTable *self, const gchar *id, GBytes *blob)
{
	g_autofree gchar *csum = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, blob);
	g_hash_table_insert(self, g_strdup(id), g_steal_pointer(&csum));
}

static void
fu_engine_integrity_measure_acpi(GHashTable *self)
{
	g_autofree gchar *path = fu_path_from_kind(FU_PATH_KIND_ACPI_TABLES);
	const gchar *tables[] = {"SLIC", "MSDM", "TPM2", NULL};

	for (guint i = 0; tables[i] != NULL; i++) {
		g_autofree gchar *fn = g_build_filename(path, tables[i], NULL);
		g_autoptr(GBytes) blob = NULL;

		blob = fu_bytes_get_contents(fn, NULL);
		if (blob != NULL && g_bytes_get_size(blob) > 0) {
			g_autofree gchar *id = g_strdup_printf("ACPI:%s", tables[i]);
			fu_engine_integrity_add_measurement(self, id, blob);
		}
	}
}

static void
fu_engine_integrity_measure_uefi(GHashTable *self)
{
	struct {
		const gchar *guid;
		const gchar *name;
	} keys[] = {{FU_EFIVAR_GUID_EFI_GLOBAL, "BootOrder"},
		    {FU_EFIVAR_GUID_EFI_GLOBAL, "BootCurrent"},
		    {FU_EFIVAR_GUID_EFI_GLOBAL, "KEK"},
		    {FU_EFIVAR_GUID_EFI_GLOBAL, "KEKDefault"},
		    {FU_EFIVAR_GUID_EFI_GLOBAL, "OsIndications"},
		    {FU_EFIVAR_GUID_EFI_GLOBAL, "OsIndicationsSupported"},
		    {FU_EFIVAR_GUID_EFI_GLOBAL, "PK"},
		    {FU_EFIVAR_GUID_EFI_GLOBAL, "PKDefault"},
		    {FU_EFIVAR_GUID_EFI_GLOBAL, "SecureBoot"},
		    {FU_EFIVAR_GUID_EFI_GLOBAL, "SetupMode"},
		    {FU_EFIVAR_GUID_EFI_GLOBAL, "SignatureSupport"},
		    {FU_EFIVAR_GUID_EFI_GLOBAL, "VendorKeys"},
		    {FU_EFIVAR_GUID_SECURITY_DATABASE, "db"},
		    {FU_EFIVAR_GUID_SECURITY_DATABASE, "dbDefault"},
		    {FU_EFIVAR_GUID_SECURITY_DATABASE, "dbx"},
		    {FU_EFIVAR_GUID_SECURITY_DATABASE, "dbxDefault"},
		    {NULL, NULL}};

	/* important keys */
	for (guint i = 0; keys[i].guid != NULL; i++) {
		g_autoptr(GBytes) blob =
		    fu_efivar_get_data_bytes(keys[i].guid, keys[i].name, NULL, NULL);
		if (blob != NULL) {
			g_autofree gchar *id = g_strdup_printf("UEFI:%s", keys[i].name);
			fu_engine_integrity_add_measurement(self, id, blob);
		}
	}

	/* Boot#### */
	for (guint i = 0; i < 0xFF; i++) {
		g_autofree gchar *name = g_strdup_printf("Boot%04X", i);
		g_autoptr(GBytes) blob =
		    fu_efivar_get_data_bytes(FU_EFIVAR_GUID_EFI_GLOBAL, name, NULL, NULL);
		if (blob != NULL && g_bytes_get_size(blob) > 0) {
			const guint8 needle[] = "f\0w\0u\0p\0d";
			g_autofree gchar *id = g_strdup_printf("UEFI:%s", name);
			if (fu_memmem_safe(g_bytes_get_data(blob, NULL),
					   g_bytes_get_size(blob),
					   needle,
					   sizeof(needle),
					   NULL,
					   NULL)) {
				g_debug("skipping %s as fwupd found", id);
				continue;
			}
			fu_engine_integrity_add_measurement(self, id, blob);
		}
	}
}

GHashTable *
fu_engine_integrity_new(GError **error)
{
	g_autoptr(GHashTable) self = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	fu_engine_integrity_measure_uefi(self);
	fu_engine_integrity_measure_acpi(self);

	/* nothing of use */
	if (g_hash_table_size(self) == 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no measurements");
		return NULL;
	}

	/* success */
	return g_steal_pointer(&self);
}

gchar *
fu_engine_integrity_to_string(GHashTable *self)
{
	GHashTableIter iter;
	gpointer key, value;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(self != NULL, NULL);

	/* sanity check */
	if (g_hash_table_size(self) == 0)
		return NULL;

	/* build into KV array */
	g_hash_table_iter_init(&iter, self);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		g_ptr_array_add(array,
				g_strdup_printf("%s=%s", (const gchar *)key, (const gchar *)value));
	}
	return fu_strjoin("\n", array);
}
