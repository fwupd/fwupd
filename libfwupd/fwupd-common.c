/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fwupd-common-private.h"
#include "fwupd-device.h"
#include "fwupd-error.h"
#include "fwupd-release.h"

#ifdef HAVE_GIO_UNIX
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <unistd.h>
#endif

#ifdef HAVE_MEMFD_CREATE
#include <sys/mman.h>
#endif

#include <locale.h>
#include <string.h>
#ifdef HAVE_UTSNAME_H
#include <sys/utsname.h>
#endif
#include <json-glib/json-glib.h>

/**
 * fwupd_checksum_guess_kind:
 * @checksum: (nullable): a checksum
 *
 * Guesses the checksum kind based on the length of the hash.
 *
 * Returns: a checksum type, e.g. %G_CHECKSUM_SHA1
 *
 * Since: 0.9.3
 **/
GChecksumType
fwupd_checksum_guess_kind(const gchar *checksum)
{
	guint len;
	if (checksum == NULL)
		return G_CHECKSUM_SHA1;
	len = strlen(checksum);
	if (len == 32)
		return G_CHECKSUM_MD5;
	if (len == 40)
		return G_CHECKSUM_SHA1;
	if (len == 64)
		return G_CHECKSUM_SHA256;
	if (len == 96)
		return G_CHECKSUM_SHA384;
	if (len == 128)
		return G_CHECKSUM_SHA512;
	return G_CHECKSUM_SHA1;
}

/**
 * fwupd_checksum_type_to_string_display:
 * @checksum_type: a #GChecksumType, e.g. %G_CHECKSUM_SHA1
 *
 * Formats a checksum type for display.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 1.9.6
 **/
const gchar *
fwupd_checksum_type_to_string_display(GChecksumType checksum_type)
{
	if (checksum_type == G_CHECKSUM_MD5)
		return "MD5";
	if (checksum_type == G_CHECKSUM_SHA1)
		return "SHA1";
	if (checksum_type == G_CHECKSUM_SHA256)
		return "SHA256";
	if (checksum_type == G_CHECKSUM_SHA384)
		return "SHA384";
	if (checksum_type == G_CHECKSUM_SHA512)
		return "SHA512";
	return NULL;
}

/**
 * fwupd_checksum_format_for_display:
 * @checksum: (nullable): a checksum
 *
 * Formats a checksum for display.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 0.9.3
 **/
gchar *
fwupd_checksum_format_for_display(const gchar *checksum)
{
	GChecksumType kind = fwupd_checksum_guess_kind(checksum);
	return g_strdup_printf("%s(%s)", fwupd_checksum_type_to_string_display(kind), checksum);
}

/**
 * fwupd_checksum_get_by_kind:
 * @checksums: (element-type utf8): checksums
 * @kind: a checksum type, e.g. %G_CHECKSUM_SHA512
 *
 * Gets a specific checksum kind.
 *
 * Returns: a checksum from the array, or %NULL if not found
 *
 * Since: 0.9.4
 **/
const gchar *
fwupd_checksum_get_by_kind(GPtrArray *checksums, GChecksumType kind)
{
	g_return_val_if_fail(checksums != NULL, NULL);
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *checksum = g_ptr_array_index(checksums, i);
		if (fwupd_checksum_guess_kind(checksum) == kind)
			return checksum;
	}
	return NULL;
}

/**
 * fwupd_checksum_get_best:
 * @checksums: (element-type utf8): checksums
 *
 * Gets a the best possible checksum kind.
 *
 * Returns: a checksum from the array, or %NULL if nothing was suitable
 *
 * Since: 0.9.4
 **/
const gchar *
fwupd_checksum_get_best(GPtrArray *checksums)
{
	GChecksumType checksum_types[] = {G_CHECKSUM_SHA512,
					  G_CHECKSUM_SHA256,
					  G_CHECKSUM_SHA384,
					  G_CHECKSUM_SHA1,
					  0};
	g_return_val_if_fail(checksums != NULL, NULL);
	for (guint i = 0; checksum_types[i] != 0; i++) {
		for (guint j = 0; j < checksums->len; j++) {
			const gchar *checksum = g_ptr_array_index(checksums, j);
			if (fwupd_checksum_guess_kind(checksum) == checksum_types[i])
				return checksum;
		}
	}
	return NULL;
}

static gchar *
fwupd_get_os_release_filename(void)
{
#ifndef _WIN32
	const gchar *hostdir = g_getenv("FWUPD_HOSTDIR");
	const gchar *sysconfdir = g_getenv("FWUPD_SYSCONFDIR");
	g_autofree gchar *fn1 = NULL;

	if (hostdir == NULL)
		hostdir = "/";

	/* override */
	if (sysconfdir != NULL) {
		g_autofree gchar *fn2 = g_build_filename(hostdir, sysconfdir, "os-release", NULL);
		if (g_file_test(fn2, G_FILE_TEST_EXISTS))
			return g_steal_pointer(&fn2);
	}

	/* host locations */
	if (g_strcmp0(sysconfdir, "/etc") != 0) {
		g_autofree gchar *fn2 = g_build_filename(hostdir, "/etc/os-release", NULL);
		if (g_file_test(fn2, G_FILE_TEST_EXISTS))
			return g_steal_pointer(&fn2);
	}
	fn1 = g_build_filename(hostdir, "/usr/lib/os-release", NULL);
	if (g_file_test(fn1, G_FILE_TEST_EXISTS))
		return g_steal_pointer(&fn1);
#endif
	return NULL;
}

/**
 * fwupd_get_os_release_full:
 * @filename: (nullable): optional filename to load
 * @error: (nullable): optional return location for an error
 *
 * Loads information from a defined system os-release file.
 *
 * Returns: (transfer container) (element-type utf8 utf8): keys from os-release
 *
 * Since: 1.8.8
 **/
GHashTable *
fwupd_get_os_release_full(const gchar *filename, GError **error)
{
	g_autofree gchar *buf = NULL;
	g_autofree gchar *filename2 = g_strdup(filename);
	g_auto(GStrv) lines = NULL;
	g_autoptr(GHashTable) hash = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	if (filename2 == NULL)
		filename2 = fwupd_get_os_release_filename();
	if (filename2 == NULL) {
#if defined(_WIN32)
		/* TODO: Read the Windows version */
		g_hash_table_insert(hash, g_strdup("OS"), g_strdup("Windows"));
#elif defined(__NetBSD__)
		g_hash_table_insert(hash, g_strdup("OS"), g_strdup("NetBSD"));
#elif defined(__OpenBSD__)
		g_hash_table_insert(hash, g_strdup("OS"), g_strdup("OpenBSD"));
#endif
		if (g_hash_table_size(hash) > 0)
			return g_steal_pointer(&hash);
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "No os-release found");
		return NULL;
	}

	/* load each line */
	if (!g_file_get_contents(filename2, &buf, NULL, error))
		return NULL;
	lines = g_strsplit(buf, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		gsize len, off = 0;
		g_auto(GStrv) split = NULL;

		/* split up into sections */
		split = g_strsplit(lines[i], "=", 2);
		if (g_strv_length(split) < 2)
			continue;

		/* remove double quotes if set both ends */
		len = strlen(split[1]);
		if (len == 0)
			continue;
		if (split[1][0] == '\"' && split[1][len - 1] == '\"') {
			off++;
			len -= 2;
		}
		g_hash_table_insert(hash, g_strdup(split[0]), g_strndup(split[1] + off, len));
	}
	return g_steal_pointer(&hash);
}

/**
 * fwupd_get_os_release:
 * @error: (nullable): optional return location for an error
 *
 * Loads information from the system os-release file.
 *
 * Returns: (transfer container) (element-type utf8 utf8): keys from os-release
 *
 * Since: 1.0.7
 **/
GHashTable *
fwupd_get_os_release(GError **error)
{
#ifdef HOST_MACHINE_SYSTEM_DARWIN
	g_autofree gchar *stdout = NULL;
	g_autofree gchar *sw_vers = g_find_program_in_path("sw_vers");
	g_auto(GStrv) split = NULL;
	g_autoptr(GHashTable) hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	struct {
		const gchar *key;
		const gchar *val;
	} kvs[] = {{"ProductName:", "NAME"},
		   {"ProductVersion:", "VERSION_ID"},
		   {"BuildVersion:", "VARIANT_ID"},
		   {NULL, NULL}};

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* macOS */
	if (sw_vers == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "No os-release found");
		return NULL;
	}

	/* parse in format:
	 *
	 *    ProductName:    Mac OS X
	 *    ProductVersion: 10.14.6
	 *    BuildVersion:   18G103
	 */
	if (!g_spawn_command_line_sync(sw_vers, &stdout, NULL, NULL, error))
		return NULL;
	split = g_strsplit(stdout, "\n", -1);
	for (guint j = 0; split[j] != NULL; j++) {
		for (guint i = 0; kvs[i].key != NULL; i++) {
			if (g_str_has_prefix(split[j], kvs[i].key)) {
				g_autofree gchar *tmp = g_strdup(split[j] + strlen(kvs[i].key));
				g_hash_table_insert(hash,
						    g_strdup(kvs[i].val),
						    g_strdup(g_strstrip(tmp)));
			}
		}
	}
	g_hash_table_insert(hash, g_strdup("ID"), g_strdup("macos"));
	return g_steal_pointer(&hash);
#else
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fwupd_get_os_release_full(NULL, error);
#endif
}

static gchar *
fwupd_build_user_agent_os_release(void)
{
	const gchar *keys[] = {"NAME", "VERSION_ID", "VARIANT", NULL};
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GPtrArray) ids_os = g_ptr_array_new();

	/* get all keys */
	hash = fwupd_get_os_release(NULL);
	if (hash == NULL)
		return NULL;

	/* create an array of the keys that exist */
	for (guint i = 0; keys[i] != NULL; i++) {
		const gchar *value = g_hash_table_lookup(hash, keys[i]);
		if (value != NULL)
			g_ptr_array_add(ids_os, (gpointer)value);
	}
	if (ids_os->len == 0)
		return NULL;
	g_ptr_array_add(ids_os, NULL);
	return g_strjoinv(" ", (gchar **)ids_os->pdata);
}

/**
 * fwupd_build_user_agent_system: (skip):
 **/
gchar *
fwupd_build_user_agent_system(void)
{
#ifdef HAVE_UTSNAME_H
	struct utsname name_tmp;
#endif
	g_autofree gchar *locale = NULL;
	g_autofree gchar *os_release = NULL;
	g_autoptr(GPtrArray) ids = g_ptr_array_new_with_free_func(g_free);

	/* system, architecture and kernel, e.g. "Linux i686 4.14.5" */
#ifdef HAVE_UTSNAME_H
	memset(&name_tmp, 0, sizeof(struct utsname));
	if (uname(&name_tmp) >= 0) {
		g_ptr_array_add(ids,
				g_strdup_printf("%s %s %s",
						name_tmp.sysname,
						name_tmp.machine,
						name_tmp.release));
	}
#endif

	/* current locale, e.g. "en-gb" */
#ifdef HAVE_LC_MESSAGES
	locale = g_strdup(setlocale(LC_MESSAGES, NULL));
#endif
	if (locale != NULL) {
		g_strdelimit(locale, ".", '\0');
		g_strdelimit(locale, "_", '-');
		g_ptr_array_add(ids, g_steal_pointer(&locale));
	}

	/* OS release, e.g. "Fedora 27 Workstation" */
	os_release = fwupd_build_user_agent_os_release();
	if (os_release != NULL)
		g_ptr_array_add(ids, g_steal_pointer(&os_release));

	/* convert to string */
	if (ids->len == 0)
		return NULL;
	g_ptr_array_add(ids, NULL);
	return g_strjoinv("; ", (gchar **)ids->pdata);
}

/**
 * fwupd_build_user_agent:
 * @package_name: (not nullable): client program name, e.g. `gnome-software`
 * @package_version: (not nullable): client program version, e.g. `3.28.1`
 *
 * Builds a user-agent to use for the download.
 *
 * Supplying harmless details to the server means it knows more about each
 * client. This allows the web service to respond in a different way, for
 * instance sending a different metadata file for old versions of fwupd, or
 * returning an error for Solaris machines.
 *
 * Before freaking out about theoretical privacy implications, much more data
 * than this is sent to each and every website you visit.
 *
 * Rather that using this function you should use [method@Client.set_user_agent_for_package]
 * which uses the *runtime* version of the daemon rather than the *build-time*
 * version.
 *
 * Returns: a string, e.g. `foo/0.1 (Linux i386 4.14.5; en; Fedora 27) fwupd/1.0.3`
 *
 * Since: 1.0.3
 **/
gchar *
fwupd_build_user_agent(const gchar *package_name, const gchar *package_version)
{
	g_autoptr(GString) str = g_string_new(NULL);
	g_autofree gchar *system = NULL;

	g_return_val_if_fail(package_name != NULL, NULL);
	g_return_val_if_fail(package_version != NULL, NULL);

	/* application name and version */
	g_string_append_printf(str, "%s/%s", package_name, package_version);

	/* system information */
	system = fwupd_build_user_agent_system();
	if (system != NULL)
		g_string_append_printf(str, " (%s)", system);

	/* platform, which in our case is just fwupd */
	if (g_strcmp0(package_name, "fwupd") != 0)
		g_string_append_printf(str, " fwupd/%s", PACKAGE_VERSION);

	/* success */
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/**
 * fwupd_build_machine_id:
 * @salt: (nullable): optional salt
 * @error: (nullable): optional return location for an error
 *
 * Gets a salted hash of the /etc/machine-id contents. This can be used to
 * identify a specific machine. It is not possible to recover the original
 * machine-id from the machine-hash.
 *
 * Returns: the SHA256 machine hash, or %NULL if the ID is not present
 *
 * Since: 1.0.4
 **/
gchar *
fwupd_build_machine_id(const gchar *salt, GError **error)
{
	const gchar *fn = NULL;
	g_autofree gchar *buf = NULL;
	g_auto(GStrv) fns = g_new0(gchar *, 6);
	g_autoptr(GChecksum) csum = NULL;
	gsize sz = 0;

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* one of these has to exist */
	fns[0] = g_build_filename(FWUPD_SYSCONFDIR, "machine-id", NULL);
	fns[1] = g_build_filename(FWUPD_LOCALSTATEDIR, "lib", "dbus", "machine-id", NULL);
	fns[2] = g_strdup("/etc/machine-id");
	fns[3] = g_strdup("/var/lib/dbus/machine-id");
	fns[4] = g_strdup("/var/db/dbus/machine-id");
	for (guint i = 0; fns[i] != NULL; i++) {
		if (g_file_test(fns[i], G_FILE_TEST_EXISTS)) {
			fn = fns[i];
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
	if (!g_file_get_contents(fn, &buf, &sz, error))
		return NULL;
	if (sz == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "The machine-id is present but unset");
		return NULL;
	}
	csum = g_checksum_new(G_CHECKSUM_SHA256);
	if (salt != NULL)
		g_checksum_update(csum, (const guchar *)salt, (gssize)strlen(salt));
	g_checksum_update(csum, (const guchar *)buf, (gssize)sz);
	return g_strdup(g_checksum_get_string(csum));
}

static void
fwupd_build_history_report_json_metadata_device(JsonBuilder *builder, FwupdDevice *dev)
{
	FwupdRelease *rel = fwupd_device_get_release_default(dev);
	GHashTable *metadata = fwupd_release_get_metadata(rel);
	g_autoptr(GList) keys = NULL;

	/* add each metadata value */
	keys = g_hash_table_get_keys(metadata);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup(metadata, key);
		json_builder_set_member_name(builder, key);
		json_builder_add_string_value(builder, value);
	}
}

static void
fwupd_build_history_report_json_device(JsonBuilder *builder, FwupdDevice *dev)
{
	FwupdRelease *rel = fwupd_device_get_release_default(dev);
	GChecksumType checksum_types[] = {G_CHECKSUM_SHA256, G_CHECKSUM_SHA1, 0};
	GPtrArray *checksums;
	GPtrArray *guids;

	/* identify the firmware used */
	checksums = fwupd_release_get_checksums(rel);
	for (guint i = 0; checksum_types[i] != 0; i++) {
		const gchar *checksum = fwupd_checksum_get_by_kind(checksums, checksum_types[i]);
		if (checksum != NULL) {
			json_builder_set_member_name(builder, "Checksum");
			json_builder_add_string_value(builder, checksum);
			break;
		}
	}

	/* identify the firmware written */
	checksums = fwupd_device_get_checksums(dev);
	if (checksums->len > 0) {
		json_builder_set_member_name(builder, "ChecksumDevice");
		json_builder_begin_array(builder);
		for (guint i = 0; i < checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index(checksums, i);
			json_builder_add_string_value(builder, checksum);
		}
		json_builder_end_array(builder);
	}

	/* allow matching the specific component */
	json_builder_set_member_name(builder, "ReleaseId");
	json_builder_add_string_value(builder, fwupd_release_get_id(rel));

	/* include the protocol used */
	if (fwupd_release_get_protocol(rel) != NULL) {
		json_builder_set_member_name(builder, "Protocol");
		json_builder_add_string_value(builder, fwupd_release_get_protocol(rel));
	}

	/* set the error state of the report */
	json_builder_set_member_name(builder, "UpdateState");
	json_builder_add_int_value(builder, fwupd_device_get_update_state(dev));
	if (fwupd_device_get_update_error(dev) != NULL) {
		json_builder_set_member_name(builder, "UpdateError");
		json_builder_add_string_value(builder, fwupd_device_get_update_error(dev));
	}
	if (fwupd_release_get_update_message(rel) != NULL) {
		json_builder_set_member_name(builder, "UpdateMessage");
		json_builder_add_string_value(builder, fwupd_release_get_update_message(rel));
	}

	/* map back to the dev type on the LVFS */
	guids = fwupd_device_get_guids(dev);
	if (guids->len > 0) {
		json_builder_set_member_name(builder, "Guid");
		json_builder_begin_array(builder);
		for (guint i = 0; i < guids->len; i++) {
			const gchar *guid = g_ptr_array_index(guids, i);
			json_builder_add_string_value(builder, guid);
		}
		json_builder_end_array(builder);
	}

	json_builder_set_member_name(builder, "Plugin");
	json_builder_add_string_value(builder, fwupd_device_get_plugin(dev));

	/* report what we're trying to update *from* and *to* */
	json_builder_set_member_name(builder, "VersionOld");
	json_builder_add_string_value(builder, fwupd_device_get_version(dev));
	json_builder_set_member_name(builder, "VersionNew");
	json_builder_add_string_value(builder, fwupd_release_get_version(rel));

	/* to know the state of the dev we're trying to update */
	json_builder_set_member_name(builder, "Flags");
	json_builder_add_int_value(builder, fwupd_device_get_flags(dev));

	/* to know when the update tried to happen, and how soon after boot */
	json_builder_set_member_name(builder, "Created");
	json_builder_add_int_value(builder, fwupd_device_get_created(dev));
	json_builder_set_member_name(builder, "Modified");
	json_builder_add_int_value(builder, fwupd_device_get_modified(dev));

	/* add saved metadata to the report */
	json_builder_set_member_name(builder, "Metadata");
	json_builder_begin_object(builder);
	fwupd_build_history_report_json_metadata_device(builder, dev);
	json_builder_end_object(builder);
}

static gboolean
fwupd_build_history_report_json_metadata(JsonBuilder *builder, GError **error)
{
	g_autoptr(GHashTable) hash = NULL;
	struct {
		const gchar *key;
		const gchar *val;
	} distro_kv[] = {{"ID", "DistroId"},
			 {"VERSION_ID", "DistroVersion"},
			 {"VARIANT_ID", "DistroVariant"},
			 {NULL, NULL}};

	/* get all required os-release keys */
	hash = fwupd_get_os_release(error);
	if (hash == NULL)
		return FALSE;
	for (guint i = 0; distro_kv[i].key != NULL; i++) {
		const gchar *tmp = g_hash_table_lookup(hash, distro_kv[i].key);
		if (tmp != NULL) {
			json_builder_set_member_name(builder, distro_kv[i].val);
			json_builder_add_string_value(builder, tmp);
		}
	}
	return TRUE;
}

/**
 * fwupd_build_history_report_json:
 * @devices: (element-type FwupdDevice): devices
 * @error: (nullable): optional return location for an error
 *
 * Builds a JSON report for the list of devices. No filtering is done on the
 * @devices array, and it is expected that the caller will filter to something
 * sane, e.g. %FWUPD_DEVICE_FLAG_REPORTED at the bare minimum.
 *
 * Returns: a string, or %NULL if the ID is not present
 *
 * Since: 1.0.4
 **/
gchar *
fwupd_build_history_report_json(GPtrArray *devices, GError **error)
{
	gchar *data;
	g_autofree gchar *machine_id = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	g_return_val_if_fail(devices != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* get a hash that represents the machine */
	machine_id = fwupd_build_machine_id("fwupd", error);
	if (machine_id == NULL)
		return NULL;

	/* create header */
	builder = json_builder_new();
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "ReportVersion");
	json_builder_add_int_value(builder, 2);
	json_builder_set_member_name(builder, "MachineId");
	json_builder_add_string_value(builder, machine_id);

	/* this is system metadata not stored in the database */
	json_builder_set_member_name(builder, "Metadata");
	json_builder_begin_object(builder);
	if (!fwupd_build_history_report_json_metadata(builder, error))
		return NULL;
	json_builder_end_object(builder);

	/* add each device */
	json_builder_set_member_name(builder, "Reports");
	json_builder_begin_array(builder);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		json_builder_begin_object(builder);
		fwupd_build_history_report_json_device(builder, dev);
		json_builder_end_object(builder);
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);

	/* export as a string */
	json_root = json_builder_get_root(builder);
	json_generator = json_generator_new();
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);
	data = json_generator_to_data(json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Failed to convert to JSON string");
		return NULL;
	}
	return data;
}

#define FWUPD_GUID_NAMESPACE_DEFAULT   "6ba7b810-9dad-11d1-80b4-00c04fd430c8"
#define FWUPD_GUID_NAMESPACE_MICROSOFT "70ffd812-4c7f-4c7d-0000-000000000000"

typedef struct __attribute__((packed)) {
	guint32 a;
	guint16 b;
	guint16 c;
	guint16 d;
	guint8 e[6];
} fwupd_guid_native_t;

/**
 * fwupd_guid_to_string:
 * @guid: a #fwupd_guid_t to read
 * @flags: GUID flags, e.g. %FWUPD_GUID_FLAG_MIXED_ENDIAN
 *
 * Returns a text GUID of mixed or BE endian for a packed buffer.
 *
 * Returns: a new GUID string
 *
 * Since: 1.2.5
 **/
gchar *
fwupd_guid_to_string(const fwupd_guid_t *guid, FwupdGuidFlags flags)
{
	fwupd_guid_native_t gnat;

	g_return_val_if_fail(guid != NULL, NULL);

	/* copy to avoid issues with aligning */
	memcpy(&gnat, guid, sizeof(gnat));

	/* mixed is bizaar, but specified as the DCE encoding */
	if (flags & FWUPD_GUID_FLAG_MIXED_ENDIAN) {
		return g_strdup_printf("%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x",
				       (guint)GUINT32_FROM_LE(gnat.a),
				       (guint)GUINT16_FROM_LE(gnat.b),
				       (guint)GUINT16_FROM_LE(gnat.c),
				       (guint)GUINT16_FROM_BE(gnat.d),
				       gnat.e[0],
				       gnat.e[1],
				       gnat.e[2],
				       gnat.e[3],
				       gnat.e[4],
				       gnat.e[5]);
	}
	return g_strdup_printf("%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x",
			       (guint)GUINT32_FROM_BE(gnat.a),
			       (guint)GUINT16_FROM_BE(gnat.b),
			       (guint)GUINT16_FROM_BE(gnat.c),
			       (guint)GUINT16_FROM_BE(gnat.d),
			       gnat.e[0],
			       gnat.e[1],
			       gnat.e[2],
			       gnat.e[3],
			       gnat.e[4],
			       gnat.e[5]);
}

/**
 * fwupd_guid_from_string:
 * @guidstr: (not nullable): a GUID, e.g. `00112233-4455-6677-8899-aabbccddeeff`
 * @guid: (nullable): a #fwupd_guid_t, or NULL to just check the GUID
 * @flags: GUID flags, e.g. %FWUPD_GUID_FLAG_MIXED_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Converts a string GUID into its binary encoding. All string GUIDs are
 * formatted as big endian but on-disk can be encoded in different ways.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.2.5
 **/
gboolean
fwupd_guid_from_string(const gchar *guidstr,
		       fwupd_guid_t *guid,
		       FwupdGuidFlags flags,
		       GError **error)
{
	fwupd_guid_native_t gu = {0x0};
	gboolean mixed_endian = flags & FWUPD_GUID_FLAG_MIXED_ENDIAN;
	guint64 tmp;
	g_auto(GStrv) split = NULL;

	g_return_val_if_fail(guidstr != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* split into sections */
	if (strlen(guidstr) != 36) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "GUID is not valid format");
		return FALSE;
	}
	split = g_strsplit(guidstr, "-", 5);
	if (g_strv_length(split) != 5) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "GUID is not valid format, no dashes");
		return FALSE;
	}
	if (strlen(split[0]) != 8 && strlen(split[1]) != 4 && strlen(split[2]) != 4 &&
	    strlen(split[3]) != 4 && strlen(split[4]) != 12) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "GUID is not valid format, not GUID");
		return FALSE;
	}

	/* parse */
	if (!g_ascii_string_to_unsigned(split[0], 16, 0, 0xffffffff, &tmp, error))
		return FALSE;
	gu.a = mixed_endian ? GUINT32_TO_LE(tmp) : GUINT32_TO_BE(tmp);
	if (!g_ascii_string_to_unsigned(split[1], 16, 0, 0xffff, &tmp, error))
		return FALSE;
	gu.b = mixed_endian ? GUINT16_TO_LE(tmp) : GUINT16_TO_BE(tmp);
	if (!g_ascii_string_to_unsigned(split[2], 16, 0, 0xffff, &tmp, error))
		return FALSE;
	gu.c = mixed_endian ? GUINT16_TO_LE(tmp) : GUINT16_TO_BE(tmp);
	if (!g_ascii_string_to_unsigned(split[3], 16, 0, 0xffff, &tmp, error))
		return FALSE;
	gu.d = GUINT16_TO_BE(tmp);
	for (guint i = 0; i < 6; i++) {
		gchar buffer[3] = {0x0};
		memcpy(buffer, split[4] + (i * 2), 2);
		if (!g_ascii_string_to_unsigned(buffer, 16, 0, 0xff, &tmp, error))
			return FALSE;
		gu.e[i] = tmp;
	}
	if (guid != NULL)
		memcpy(guid, &gu, sizeof(gu));

	/* success */
	return TRUE;
}

/**
 * fwupd_guid_hash_data:
 * @data: data to hash
 * @datasz: length of @data
 * @flags: GUID flags, e.g. %FWUPD_GUID_FLAG_NAMESPACE_MICROSOFT
 *
 * Returns a GUID for some data. This uses a hash and so even small
 * differences in the @data will produce radically different return values.
 *
 * The implementation is taken from RFC4122, Section 4.1.3; specifically
 * using a type-5 SHA-1 hash.
 *
 * Returns: a new GUID, or %NULL for internal error
 *
 * Since: 1.2.5
 **/
gchar *
fwupd_guid_hash_data(const guint8 *data, gsize datasz, FwupdGuidFlags flags)
{
	gsize digestlen = 20;
	guint8 hash[20];
	fwupd_guid_t uu_new;
	g_autoptr(GChecksum) csum = NULL;
	const fwupd_guid_t uu_default = {0x6b,
					 0xa7,
					 0xb8,
					 0x10,
					 0x9d,
					 0xad,
					 0x11,
					 0xd1,
					 0x80,
					 0xb4,
					 0x00,
					 0xc0,
					 0x4f,
					 0xd4,
					 0x30,
					 0xc8};
	const fwupd_guid_t uu_microso = {0x70, 0xff, 0xd8, 0x12, 0x4c, 0x7f, 0x4c, 0x7d};
	const fwupd_guid_t *uu_namespace = &uu_default;

	g_return_val_if_fail(data != NULL, NULL);
	g_return_val_if_fail(datasz != 0, NULL);

	/* old MS GUID */
	if (flags & FWUPD_GUID_FLAG_NAMESPACE_MICROSOFT)
		uu_namespace = &uu_microso;

	/* hash the namespace and then the string */
	csum = g_checksum_new(G_CHECKSUM_SHA1);
	g_checksum_update(csum, (guchar *)uu_namespace, sizeof(*uu_namespace));
	g_checksum_update(csum, (guchar *)data, (gssize)datasz);
	g_checksum_get_digest(csum, hash, &digestlen);

	/* copy most parts of the hash 1:1 */
	memcpy(uu_new, hash, sizeof(uu_new));

	/* set specific bits according to Section 4.1.3 */
	uu_new[6] = (guint8)((uu_new[6] & 0x0f) | (5 << 4));
	uu_new[8] = (guint8)((uu_new[8] & 0x3f) | 0x80);
	return fwupd_guid_to_string((const fwupd_guid_t *)&uu_new, flags);
}

/**
 * fwupd_device_id_is_valid:
 * @device_id: string to check, e.g. `d3fae86d95e5d56626129d00e332c4b8dac95442`
 *
 * Checks the string is a valid non-partial device ID. It is important to note
 * that the wildcard ID of `*` is not considered a valid ID in this function and
 * the client must check for this manually if this should be allowed.
 *
 * Returns: %TRUE if @guid was a fwupd device ID, %FALSE otherwise
 *
 * Since: 1.4.1
 **/
gboolean
fwupd_device_id_is_valid(const gchar *device_id)
{
	if (device_id == NULL)
		return FALSE;
	if (strlen(device_id) != 40)
		return FALSE;
	for (guint i = 0; device_id[i] != '\0'; i++) {
		gchar tmp = device_id[i];
		/* isalnum isn't case specific */
		if ((tmp < 'a' || tmp > 'f') && (tmp < '0' || tmp > '9'))
			return FALSE;
	}
	return TRUE;
}

/**
 * fwupd_guid_is_valid:
 * @guid: string to check, e.g. `00112233-4455-6677-8899-aabbccddeeff`
 *
 * Checks the string is a valid GUID.
 *
 * Returns: %TRUE if @guid was a valid GUID, %FALSE otherwise
 *
 * Since: 1.2.5
 **/
gboolean
fwupd_guid_is_valid(const gchar *guid)
{
	const gchar zeroguid[] = {"00000000-0000-0000-0000-000000000000"};

	/* sanity check */
	if (guid == NULL)
		return FALSE;

	/* check for dashes and hexdigits in the right place */
	for (guint i = 0; i < sizeof(zeroguid) - 1; i++) {
		if (guid[i] == '\0')
			return FALSE;
		if (zeroguid[i] == '-') {
			if (guid[i] != '-')
				return FALSE;
			continue;
		}
		if (!g_ascii_isxdigit(guid[i]))
			return FALSE;
	}

	/* longer than required */
	if (guid[sizeof(zeroguid) - 1] != '\0')
		return FALSE;

	/* not valid */
	return g_strcmp0(guid, zeroguid) != 0;
}

/**
 * fwupd_guid_hash_string:
 * @str: (nullable): a source string to use as a key
 *
 * Returns a GUID for a given string. This uses a hash and so even small
 * differences in the @str will produce radically different return values.
 *
 * The default implementation is taken from RFC4122, Section 4.1.3; specifically
 * using a type-5 SHA-1 hash with a DNS namespace.
 * The same result can be obtained with this simple python program:
 *
 *    #!/usr/bin/python
 *    import uuid
 *    print uuid.uuid5(uuid.NAMESPACE_DNS, 'python.org')
 *
 * Returns: a new GUID, or %NULL if the string was invalid
 *
 * Since: 1.2.5
 **/
gchar *
fwupd_guid_hash_string(const gchar *str)
{
	if (str == NULL || str[0] == '\0')
		return NULL;
	return fwupd_guid_hash_data((const guint8 *)str, strlen(str), FWUPD_GUID_FLAG_NONE);
}

/**
 * fwupd_hash_kv_to_variant: (skip):
 **/
GVariant *
fwupd_hash_kv_to_variant(GHashTable *hash)
{
	GVariantBuilder builder;
	g_autoptr(GList) keys = g_hash_table_get_keys(hash);
	g_variant_builder_init(&builder, G_VARIANT_TYPE_ARRAY);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup(hash, key);
		g_variant_builder_add(&builder, "{ss}", key, value);
	}
	return g_variant_builder_end(&builder);
}

/**
 * fwupd_variant_to_hash_kv: (skip):
 **/
GHashTable *
fwupd_variant_to_hash_kv(GVariant *dict)
{
	GHashTable *hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	GVariantIter iter;
	const gchar *key;
	const gchar *value;
	g_variant_iter_init(&iter, dict);
	while (g_variant_iter_loop(&iter, "{&s&s}", &key, &value))
		g_hash_table_insert(hash, g_strdup(key), g_strdup(value));
	return hash;
}

static void
fwupd_input_stream_read_bytes_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
	GByteArray *bufarr;
	GInputStream *stream = G_INPUT_STREAM(source);
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	guint8 *buf;
	gsize bufsz = 0;

	/* read buf */
	bytes = g_input_stream_read_bytes_finish(stream, res, &error);
	if (bytes == NULL) {
		g_task_return_error(task, g_steal_pointer(&error));
		return;
	}

	/* add bytes to buffer */
	bufarr = g_task_get_task_data(task);
	if (g_bytes_get_size(bytes) > 0) {
		GCancellable *cancellable = g_task_get_cancellable(task);
		g_debug("add %u", (guint)g_bytes_get_size(bytes));
		g_byte_array_append(bufarr, g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
		g_input_stream_read_bytes_async(g_steal_pointer(&stream),
						256 * 1024, /* bigger chunk */
						G_PRIORITY_DEFAULT,
						cancellable,
						fwupd_input_stream_read_bytes_cb,
						g_steal_pointer(&task));
		return;
	}

	/* success */
	buf = g_byte_array_steal(bufarr, &bufsz);
	g_task_return_pointer(task, g_bytes_new_take(buf, bufsz), (GDestroyNotify)g_bytes_unref);
}

/**
 * fwupd_input_stream_read_bytes_async: (skip):
 **/
void
fwupd_input_stream_read_bytes_async(GInputStream *stream,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer callback_data)
{
	g_autoptr(GTask) task = NULL;

	g_return_if_fail(G_IS_INPUT_STREAM(stream));
	g_return_if_fail(cancellable == NULL || G_IS_CANCELLABLE(cancellable));

	task = g_task_new(stream, cancellable, callback, callback_data);
	g_task_set_task_data(task, g_byte_array_new(), (GDestroyNotify)g_byte_array_unref);
	g_input_stream_read_bytes_async(stream,
					64 * 1024, /* small */
					G_PRIORITY_DEFAULT,
					cancellable,
					fwupd_input_stream_read_bytes_cb,
					g_steal_pointer(&task));
}

/**
 * fwupd_input_stream_read_bytes_finish: (skip):
 **/
GBytes *
fwupd_input_stream_read_bytes_finish(GInputStream *stream, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(g_task_is_valid(res, stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_task_propagate_pointer(G_TASK(res), error);
}

#ifdef HAVE_GIO_UNIX
/**
 * fwupd_unix_input_stream_from_bytes: (skip):
 **/
GUnixInputStream *
fwupd_unix_input_stream_from_bytes(GBytes *bytes, GError **error)
{
	gint fd;
	gssize rc;
#ifndef HAVE_MEMFD_CREATE
	gchar tmp_file[] = "/tmp/fwupd.XXXXXX";
#endif

#ifdef HAVE_MEMFD_CREATE
	fd = memfd_create("fwupd", MFD_CLOEXEC);
#else
	/* emulate in-memory file by an unlinked temporary file */
	fd = g_mkstemp(tmp_file);
	if (fd != -1) {
		rc = g_unlink(tmp_file);
		if (rc != 0) {
			if (!g_close(fd, error)) {
				g_prefix_error(error, "failed to close temporary file: ");
				return NULL;
			}
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "failed to unlink temporary file");
			return NULL;
		}
	}
#endif

	if (fd < 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "failed to create memfd");
		return NULL;
	}
	rc = write(fd, g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to write %" G_GSSIZE_FORMAT,
			    rc);
		return NULL;
	}
	if (lseek(fd, 0, SEEK_SET) < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "failed to seek: %s",
			    g_strerror(errno));
		return NULL;
	}
	return G_UNIX_INPUT_STREAM(g_unix_input_stream_new(fd, TRUE));
}

/**
 * fwupd_unix_input_stream_from_fn: (skip):
 **/
GUnixInputStream *
fwupd_unix_input_stream_from_fn(const gchar *fn, GError **error)
{
	gint fd = open(fn, O_RDONLY);
	if (fd < 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "failed to open %s", fn);
		return NULL;
	}
	return G_UNIX_INPUT_STREAM(g_unix_input_stream_new(fd, TRUE));
}
#endif

/**
 * fwupd_common_json_add_string: (skip):
 **/
void
fwupd_common_json_add_string(JsonBuilder *builder, const gchar *key, const gchar *value)
{
	if (value == NULL)
		return;
	json_builder_set_member_name(builder, key);
	json_builder_add_string_value(builder, value);
}

/**
 * fwupd_common_json_add_int: (skip):
 **/
void
fwupd_common_json_add_int(JsonBuilder *builder, const gchar *key, guint64 value)
{
	json_builder_set_member_name(builder, key);
	json_builder_add_int_value(builder, value);
}

/**
 * fwupd_common_json_add_boolean: (skip):
 **/
void
fwupd_common_json_add_boolean(JsonBuilder *builder, const gchar *key, gboolean value)
{
	json_builder_set_member_name(builder, key);
	json_builder_add_string_value(builder, value ? "true" : "false");
}

/**
 * fwupd_common_json_add_stringv: (skip):
 **/
void
fwupd_common_json_add_stringv(JsonBuilder *builder, const gchar *key, gchar **value)
{
	if (value == NULL)
		return;
	json_builder_set_member_name(builder, key);
	json_builder_begin_array(builder);
	for (guint i = 0; value[i] != NULL; i++)
		json_builder_add_string_value(builder, value[i]);
	json_builder_end_array(builder);
}

/**
 * fwupd_pad_kv_str: (skip):
 **/
void
fwupd_pad_kv_str(GString *str, const gchar *key, const gchar *value)
{
	/* ignore */
	if (key == NULL || value == NULL)
		return;
	g_string_append_printf(str, "  %s: ", key);
	for (gsize i = strlen(key); i < 20; i++)
		g_string_append(str, " ");
	g_string_append_printf(str, "%s\n", value);
}

/**
 * fwupd_pad_kv_unx: (skip):
 **/
void
fwupd_pad_kv_unx(GString *str, const gchar *key, guint64 value)
{
	g_autoptr(GDateTime) date = NULL;
	g_autofree gchar *tmp = NULL;

	/* ignore */
	if (value == 0)
		return;

	date = g_date_time_new_from_unix_utc((gint64)value);
	tmp = g_date_time_format(date, "%F");
	fwupd_pad_kv_str(str, key, tmp);
}

/**
 * fwupd_pad_kv_int: (skip):
 **/
void
fwupd_pad_kv_int(GString *str, const gchar *key, guint32 value)
{
	g_autofree gchar *tmp = NULL;

	/* ignore */
	if (value == 0)
		return;
	tmp = g_strdup_printf("%" G_GUINT32_FORMAT, value);
	fwupd_pad_kv_str(str, key, tmp);
}
