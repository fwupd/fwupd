/*
 * Copyright 2020 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
			       const gchar *host_bkc,
			       GError **error)
{
	g_autoptr(GPtrArray) rels = NULL;
	g_auto(GStrv) host_bkcs = g_strsplit(host_bkc, ",", -1);

	/* find the newest release that matches */
	rels = fu_engine_get_releases(self, request, fwupd_device_get_id(dev), error);
	if (rels == NULL)
		return NULL;
	for (guint i = 0; i < rels->len; i++) {
		FwupdRelease *rel = g_ptr_array_index(rels, i);
		for (guint j = 0; host_bkcs[j] != NULL; j++) {
			if (fwupd_release_has_tag(rel, host_bkcs[j]))
				return g_object_ref(rel);
		}
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
	request = fu_engine_request_new(NULL);
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
		g_string_append(str, "\n");
		g_string_append_printf(str,
				       /* TRANSLATORS: this is shown in the MOTD -- %1 is the
					* command name, e.g. `fwupdmgr sync` */
				       _("Run `%s` to complete this action."),
				       "fwupdmgr sync");
		g_string_append(str, "\n\n");
	} else if (upgrade_count > 0) {
		g_string_append(str, "\n");
		g_string_append_printf(str,
				       /* TRANSLATORS: this is shown in the MOTD */
				       ngettext("%u device has a firmware upgrade available.",
						"%u devices have a firmware upgrade available.",
						upgrade_count),
				       upgrade_count);
		g_string_append(str, "\n");
		g_string_append_printf(str,
				       /* TRANSLATORS: this is shown in the MOTD -- %1 is the
					* command name, e.g. `fwupdmgr get-upgrades` */
				       _("Run `%s` for more information."),
				       "fwupdmgr get-upgrades");
		g_string_append(str, "\n\n");
	}

	/* success, with an empty file if nothing to say */
	g_debug("writing motd target %s", target);
	return g_file_set_contents(target, str->str, str->len, error);
}

gboolean
fu_engine_update_devices_file(FuEngine *self, GError **error)
{
	FwupdCodecFlags flags = FWUPD_CODEC_FLAG_NONE;
	gsize len;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) generator = NULL;
	g_autoptr(JsonNode) root = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autofree gchar *data = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar *target = NULL;

	if (fu_engine_config_get_show_device_private(fu_engine_get_config(self)))
		flags |= FWUPD_CODEC_FLAG_TRUSTED;

	builder = json_builder_new();
	json_builder_begin_object(builder);

	devices = fu_engine_get_devices(self, NULL);
	if (devices != NULL)
		fwupd_codec_array_to_json(devices, "Devices", builder, flags);

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
fu_engine_integrity_measure_acpi(FuContext *ctx, GHashTable *self)
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
fu_engine_integrity_measure_uefi(FuContext *ctx, GHashTable *self)
{
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	struct {
		const gchar *guid;
		const gchar *name;
	} keys[] = {{FU_EFIVARS_GUID_EFI_GLOBAL, "BootOrder"},
		    {FU_EFIVARS_GUID_EFI_GLOBAL, "BootCurrent"},
		    {FU_EFIVARS_GUID_EFI_GLOBAL, "KEK"},
		    {FU_EFIVARS_GUID_EFI_GLOBAL, "KEKDefault"},
		    {FU_EFIVARS_GUID_EFI_GLOBAL, "OsIndications"},
		    {FU_EFIVARS_GUID_EFI_GLOBAL, "OsIndicationsSupported"},
		    {FU_EFIVARS_GUID_EFI_GLOBAL, "PK"},
		    {FU_EFIVARS_GUID_EFI_GLOBAL, "PKDefault"},
		    {FU_EFIVARS_GUID_EFI_GLOBAL, "SecureBoot"},
		    {FU_EFIVARS_GUID_EFI_GLOBAL, "SetupMode"},
		    {FU_EFIVARS_GUID_EFI_GLOBAL, "SignatureSupport"},
		    {FU_EFIVARS_GUID_EFI_GLOBAL, "VendorKeys"},
		    {FU_EFIVARS_GUID_SECURITY_DATABASE, "db"},
		    {FU_EFIVARS_GUID_SECURITY_DATABASE, "dbDefault"},
		    {FU_EFIVARS_GUID_SECURITY_DATABASE, "dbx"},
		    {FU_EFIVARS_GUID_SECURITY_DATABASE, "dbxDefault"},
		    {NULL, NULL}};

	/* important keys */
	for (guint i = 0; keys[i].guid != NULL; i++) {
		g_autoptr(GBytes) blob =
		    fu_efivars_get_data_bytes(efivars, keys[i].guid, keys[i].name, NULL, NULL);
		if (blob != NULL) {
			g_autofree gchar *id = g_strdup_printf("UEFI:%s", keys[i].name);
			fu_engine_integrity_add_measurement(self, id, blob);
		}
	}

	/* Boot#### */
	for (guint i = 0; i < 0xFF; i++) {
		g_autoptr(GBytes) blob = fu_efivars_get_boot_data(efivars, i, NULL);
		if (blob != NULL && g_bytes_get_size(blob) > 0) {
			const guint8 needle[] = "f\0w\0u\0p\0d";
			g_autofree gchar *id = g_strdup_printf("UEFI:Boot%04X", i);
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
fu_engine_integrity_new(FuContext *ctx, GError **error)
{
	g_autoptr(GHashTable) self = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	fu_engine_integrity_measure_uefi(ctx, self);
	fu_engine_integrity_measure_acpi(ctx, self);

	/* nothing of use */
	if (g_hash_table_size(self) == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no measurements");
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

static const GError *
fu_engine_error_array_find(GPtrArray *errors, FwupdError error_code)
{
	for (guint j = 0; j < errors->len; j++) {
		const GError *error = g_ptr_array_index(errors, j);
		if (g_error_matches(error, FWUPD_ERROR, error_code))
			return error;
	}
	return NULL;
}

static guint
fu_engine_error_array_count(GPtrArray *errors, FwupdError error_code)
{
	guint cnt = 0;
	for (guint j = 0; j < errors->len; j++) {
		const GError *error = g_ptr_array_index(errors, j);
		if (g_error_matches(error, FWUPD_ERROR, error_code))
			cnt++;
	}
	return cnt;
}

static gboolean
fu_engine_error_array_matches_any(GPtrArray *errors, FwupdError *error_codes)
{
	for (guint j = 0; j < errors->len; j++) {
		const GError *error = g_ptr_array_index(errors, j);
		gboolean matches_any = FALSE;
		for (guint i = 0; error_codes[i] != FWUPD_ERROR_LAST; i++) {
			if (g_error_matches(error, FWUPD_ERROR, error_codes[i])) {
				matches_any = TRUE;
				break;
			}
		}
		if (!matches_any)
			return FALSE;
	}
	return TRUE;
}

/**
 * fu_engine_error_array_get_best:
 * @errors: (element-type GError): array of errors
 *
 * Finds the 'best' error to show the user from a array of errors, creating a
 * completely bespoke error where required.
 *
 * Returns: (transfer full): a #GError, never %NULL
 **/
GError *
fu_engine_error_array_get_best(GPtrArray *errors)
{
	FwupdError err_prio[] = {FWUPD_ERROR_INVALID_FILE,
				 FWUPD_ERROR_VERSION_SAME,
				 FWUPD_ERROR_VERSION_NEWER,
				 FWUPD_ERROR_NOT_SUPPORTED,
				 FWUPD_ERROR_INTERNAL,
				 FWUPD_ERROR_NOT_FOUND,
				 FWUPD_ERROR_LAST};
	FwupdError err_all_uptodate[] = {FWUPD_ERROR_VERSION_SAME,
					 FWUPD_ERROR_NOT_FOUND,
					 FWUPD_ERROR_NOT_SUPPORTED,
					 FWUPD_ERROR_LAST};
	FwupdError err_all_newer[] = {FWUPD_ERROR_VERSION_NEWER,
				      FWUPD_ERROR_VERSION_SAME,
				      FWUPD_ERROR_NOT_FOUND,
				      FWUPD_ERROR_NOT_SUPPORTED,
				      FWUPD_ERROR_LAST};

	/* are all the errors either GUID-not-matched or version-same? */
	if (fu_engine_error_array_count(errors, FWUPD_ERROR_VERSION_SAME) > 1 &&
	    fu_engine_error_array_matches_any(errors, err_all_uptodate)) {
		return g_error_new(FWUPD_ERROR,
				   FWUPD_ERROR_NOTHING_TO_DO,
				   "All updatable firmware is already installed");
	}

	/* are all the errors either GUID-not-matched or version same or newer? */
	if (fu_engine_error_array_count(errors, FWUPD_ERROR_VERSION_NEWER) > 1 &&
	    fu_engine_error_array_matches_any(errors, err_all_newer)) {
		return g_error_new(FWUPD_ERROR,
				   FWUPD_ERROR_NOTHING_TO_DO,
				   "All updatable devices already have newer versions");
	}

	/* get the most important single error */
	for (guint i = 0; err_prio[i] != FWUPD_ERROR_LAST; i++) {
		const GError *error_tmp = fu_engine_error_array_find(errors, err_prio[i]);
		if (error_tmp != NULL)
			return g_error_copy(error_tmp);
	}

	/* fall back to something */
	return g_error_new(FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "No supported devices found");
}

/**
 * fu_engine_build_machine_id:
 * @salt: (nullable): optional salt
 * @error: (nullable): optional return location for an error
 *
 * Gets a salted hash of the /etc/machine-id contents. This can be used to
 * identify a specific machine. It is not possible to recover the original
 * machine-id from the machine-hash.
 *
 * Returns: the SHA256 machine hash, or %NULL if the ID is not present
 **/
gchar *
fu_engine_build_machine_id(const gchar *salt, GError **error)
{
	const gchar *machine_id;
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autoptr(GChecksum) csum = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* in test mode */
	machine_id = g_getenv("FWUPD_MACHINE_ID");
	if (machine_id != NULL) {
		buf = g_strdup(machine_id);
		bufsz = strlen(buf);
	} else {
		const gchar *fn = NULL;
		g_autoptr(GPtrArray) fns = g_ptr_array_new_with_free_func(g_free);

		/* one of these has to exist */
		g_ptr_array_add(fns, g_build_filename(FWUPD_SYSCONFDIR, "machine-id", NULL));
		g_ptr_array_add(
		    fns,
		    g_build_filename(FWUPD_LOCALSTATEDIR, "lib", "dbus", "machine-id", NULL));
		g_ptr_array_add(fns, g_strdup("/etc/machine-id"));
		g_ptr_array_add(fns, g_strdup("/var/lib/dbus/machine-id"));
		g_ptr_array_add(fns, g_strdup("/var/db/dbus/machine-id"));
		/* this is the hardcoded path for homebrew, e.g. `sudo dbus-uuidgen --ensure` */
		g_ptr_array_add(fns, g_strdup("/usr/local/var/lib/dbus/machine-id"));
		for (guint i = 0; i < fns->len; i++) {
			const gchar *fn_tmp = g_ptr_array_index(fns, i);
			if (g_file_test(fn_tmp, G_FILE_TEST_EXISTS)) {
				fn = fn_tmp;
				break;
			}
		}
		if (fn == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "The machine-id is not present");
			return NULL;
		}
		if (!g_file_get_contents(fn, &buf, &bufsz, error))
			return NULL;
		if (bufsz == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "The machine-id is present but unset");
			return NULL;
		}
	}
	csum = g_checksum_new(G_CHECKSUM_SHA256);
	if (salt != NULL)
		g_checksum_update(csum, (const guchar *)salt, (gssize)strlen(salt));
	g_checksum_update(csum, (const guchar *)buf, (gssize)bufsz);
	return g_strdup(g_checksum_get_string(csum));
}
