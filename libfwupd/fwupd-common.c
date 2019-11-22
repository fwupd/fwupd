/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fwupd-common-private.h"
#include "fwupd-device.h"
#include "fwupd-error.h"
#include "fwupd-release.h"

#include <locale.h>
#include <string.h>
#ifdef HAVE_UTSNAME_H
#include <sys/utsname.h>
#endif
#include <json-glib/json-glib.h>

#if !GLIB_CHECK_VERSION(2,54,0)
#include <errno.h>
#endif

/**
 * fwupd_checksum_guess_kind:
 * @checksum: A checksum
 *
 * Guesses the checksum kind based on the length of the hash.
 *
 * Returns: a #GChecksumType, e.g. %G_CHECKSUM_SHA1
 *
 * Since: 0.9.3
 **/
GChecksumType
fwupd_checksum_guess_kind (const gchar *checksum)
{
	guint len;
	if (checksum == NULL)
		return G_CHECKSUM_SHA1;
	len = strlen (checksum);
	if (len == 32)
		return G_CHECKSUM_MD5;
	if (len == 40)
		return G_CHECKSUM_SHA1;
	if (len == 64)
		return G_CHECKSUM_SHA256;
	if (len == 128)
		return G_CHECKSUM_SHA512;
	return G_CHECKSUM_SHA1;
}

static const gchar *
fwupd_checksum_type_to_string_display (GChecksumType checksum_type)
{
	if (checksum_type == G_CHECKSUM_MD5)
		return "MD5";
	if (checksum_type == G_CHECKSUM_SHA1)
		return "SHA1";
	if (checksum_type == G_CHECKSUM_SHA256)
		return "SHA256";
	if (checksum_type == G_CHECKSUM_SHA512)
		return "SHA512";
	return NULL;
}

/**
 * fwupd_checksum_format_for_display:
 * @checksum: A checksum
 *
 * Formats a checksum for display.
 *
 * Returns: text, or %NULL for invalid
 *
 * Since: 0.9.3
 **/
gchar *
fwupd_checksum_format_for_display (const gchar *checksum)
{
	GChecksumType kind = fwupd_checksum_guess_kind (checksum);
	return g_strdup_printf ("%s(%s)",
				fwupd_checksum_type_to_string_display (kind),
				checksum);
}

/**
 * fwupd_checksum_get_by_kind:
 * @checksums: (element-type utf8): checksums
 * @kind: a #GChecksumType, e.g. %G_CHECKSUM_SHA512
 *
 * Gets a specific checksum kind.
 *
 * Returns: a checksum from the array, or %NULL if not found
 *
 * Since: 0.9.4
 **/
const gchar *
fwupd_checksum_get_by_kind (GPtrArray *checksums, GChecksumType kind)
{
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *checksum = g_ptr_array_index (checksums, i);
		if (fwupd_checksum_guess_kind (checksum) == kind)
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
fwupd_checksum_get_best (GPtrArray *checksums)
{
	GChecksumType checksum_types[] = {
		G_CHECKSUM_SHA512,
		G_CHECKSUM_SHA256,
		G_CHECKSUM_SHA1,
		0 };
	for (guint i = 0; checksum_types[i] != 0; i++) {
		for (guint j = 0; j < checksums->len; j++) {
			const gchar *checksum = g_ptr_array_index (checksums, j);
			if (fwupd_checksum_guess_kind (checksum) == checksum_types[i])
				return checksum;
		}
	}
	return NULL;
}

/**
 * fwupd_get_os_release:
 * @error: A #GError or %NULL
 *
 * Loads information from the system os-release file.
 *
 * Returns: (transfer container) (element-type utf8 utf8): keys from os-release
 *
 * Since: 1.0.7
 **/
GHashTable *
fwupd_get_os_release (GError **error)
{
	GHashTable *hash;
	const gchar *filename = NULL;
	const gchar *paths[] = { "/etc/os-release", "/usr/lib/os-release", NULL };
	g_autofree gchar *buf = NULL;
	g_auto(GStrv) lines = NULL;

/* TODO: Read the Windows version */
#ifdef _WIN32
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	g_hash_table_insert (hash,
			     g_strdup("OS"),
			     g_strdup("Windows"));
	return hash;
#endif

	/* find the correct file */
	for (guint i = 0; paths[i] != NULL; i++) {
		g_debug ("looking for os-release at %s", paths[i]);
		if (g_file_test (paths[i], G_FILE_TEST_EXISTS)) {
			filename = paths[i];
			break;
		}
	}
	if (filename == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "No os-release found");
		return NULL;
	}

	/* load each line */
	if (!g_file_get_contents (filename, &buf, NULL, error))
		return NULL;
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	lines = g_strsplit (buf, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		gsize len, off = 0;
		g_auto(GStrv) split = NULL;

		/* split up into sections */
		split = g_strsplit (lines[i], "=", 2);
		if (g_strv_length (split) < 2)
			continue;

		/* remove double quotes if set both ends */
		len = strlen (split[1]);
		if (len == 0)
			continue;
		if (split[1][0] == '\"' && split[1][len-1] == '\"') {
			off++;
			len -= 2;
		}
		g_hash_table_insert (hash,
				     g_strdup (split[0]),
				     g_strndup (split[1] + off, len));
	}
	return hash;
}

static gchar *
fwupd_build_user_agent_os_release (void)
{
	const gchar *keys[] = { "NAME", "VERSION_ID", "VARIANT", NULL };
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GPtrArray) ids_os = g_ptr_array_new ();

	/* get all keys */
	hash = fwupd_get_os_release (NULL);
	if (hash == NULL)
		return NULL;

	/* create an array of the keys that exist */
	for (guint i = 0; keys[i] != NULL; i++) {
		const gchar *value = g_hash_table_lookup (hash, keys[i]);
		if (value != NULL)
			g_ptr_array_add (ids_os, (gpointer) value);
	}
	if (ids_os->len == 0)
		return NULL;
	g_ptr_array_add (ids_os, NULL);
	return g_strjoinv (" ", (gchar **) ids_os->pdata);
}

static gchar *
fwupd_build_user_agent_system (void)
{
#ifdef HAVE_UTSNAME_H
	struct utsname name_tmp;
#endif
	g_autofree gchar *locale = NULL;
	g_autofree gchar *os_release = NULL;
	g_autoptr(GPtrArray) ids = g_ptr_array_new_with_free_func (g_free);

	/* system, architecture and kernel, e.g. "Linux i686 4.14.5" */
#ifdef HAVE_UTSNAME_H
	memset (&name_tmp, 0, sizeof(struct utsname));
	if (uname (&name_tmp) >= 0) {
		g_ptr_array_add (ids, g_strdup_printf ("%s %s %s",
						       name_tmp.sysname,
						       name_tmp.machine,
						       name_tmp.release));
	}
#endif

	/* current locale, e.g. "en-gb" */
#ifdef HAVE_LC_MESSAGES
	locale = g_strdup (setlocale (LC_MESSAGES, NULL));
#endif
	if (locale != NULL) {
		g_strdelimit (locale, ".", '\0');
		g_strdelimit (locale, "_", '-');
		g_ptr_array_add (ids, g_steal_pointer (&locale));
	}

	/* OS release, e.g. "Fedora 27 Workstation" */
	os_release = fwupd_build_user_agent_os_release ();
	if (os_release != NULL)
		g_ptr_array_add (ids, g_steal_pointer (&os_release));

	/* convert to string */
	if (ids->len == 0)
		return NULL;
	g_ptr_array_add (ids, NULL);
	return g_strjoinv ("; ", (gchar **) ids->pdata);
}

/**
 * fwupd_build_user_agent:
 * @package_name: client program name, e.g. "gnome-software"
 * @package_version: client program version, e.g. "3.28.1"
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
 * Returns: a string, e.g. `foo/0.1 (Linux i386 4.14.5; en; Fedora 27) fwupd/1.0.3`
 *
 * Since: 1.0.3
 **/
gchar *
fwupd_build_user_agent (const gchar *package_name, const gchar *package_version)
{
	GString *str = g_string_new (NULL);
	g_autofree gchar *system = NULL;

	/* application name and version */
	g_string_append_printf (str, "%s/%s", package_name, package_version);

	/* system information */
	system = fwupd_build_user_agent_system ();
	if (system != NULL)
		g_string_append_printf (str, " (%s)", system);

	/* platform, which in our case is just fwupd */
	if (g_strcmp0 (package_name, "fwupd") != 0)
		g_string_append_printf (str, " fwupd/%s", PACKAGE_VERSION);

	/* success */
	return g_string_free (str, FALSE);
}

/**
 * fwupd_build_machine_id:
 * @salt: The salt, or %NULL for none
 * @error: A #GError or %NULL
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
fwupd_build_machine_id (const gchar *salt, GError **error)
{
	const gchar *fn = NULL;
	g_autofree gchar *buf = NULL;
	g_auto(GStrv) fns = g_new0 (gchar *, 5);
	g_autoptr(GChecksum) csum = NULL;
	gsize sz = 0;

	/* one of these has to exist */
	fns[0] = g_build_filename (FWUPD_SYSCONFDIR, "machine-id", NULL);
	fns[1] = g_build_filename (FWUPD_LOCALSTATEDIR, "lib", "dbus", "machine-id", NULL);
	fns[2] = g_strdup ("/etc/machine-id");
	fns[3] = g_strdup ("/var/lib/dbus/machine-id");
	for (guint i = 0; fns[i] != NULL; i++) {
		if (g_file_test (fns[i], G_FILE_TEST_EXISTS)) {
			fn = fns[i];
			break;
		}
	}
	if (fn == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "The machine-id is not present");
		return NULL;
	}
	if (!g_file_get_contents (fn, &buf, &sz, error))
		return NULL;
	if (sz == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "The machine-id is present but unset");
		return NULL;
	}
	csum = g_checksum_new (G_CHECKSUM_SHA256);
	if (salt != NULL)
		g_checksum_update (csum, (const guchar *) salt, (gssize) strlen (salt));
	g_checksum_update (csum, (const guchar *) buf, (gssize) sz);
	return g_strdup (g_checksum_get_string (csum));
}

static void
fwupd_build_history_report_json_metadata_device (JsonBuilder *builder, FwupdDevice *dev)
{
	FwupdRelease *rel = fwupd_device_get_release_default (dev);
	GHashTable *metadata = fwupd_release_get_metadata (rel);
	g_autoptr(GList) keys = NULL;

	/* add each metadata value */
	keys = g_hash_table_get_keys (metadata);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup (metadata, key);
		json_builder_set_member_name (builder, key);
		json_builder_add_string_value (builder, value);
	}
}

static void
fwupd_build_history_report_json_device (JsonBuilder *builder, FwupdDevice *dev)
{
	FwupdRelease *rel = fwupd_device_get_release_default (dev);
	GPtrArray *checksums;
	GPtrArray *guids;

	/* identify the firmware used */
	json_builder_set_member_name (builder, "Checksum");
	checksums = fwupd_release_get_checksums (rel);
	json_builder_add_string_value (builder, fwupd_checksum_get_by_kind (checksums, G_CHECKSUM_SHA1));

	/* identify the firmware written */
	checksums = fwupd_device_get_checksums (dev);
	if (checksums->len > 0) {
		json_builder_set_member_name (builder, "ChecksumDevice");
		json_builder_begin_array (builder);
		for (guint i = 0; i < checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index (checksums, i);
			json_builder_add_string_value (builder, checksum);
		}
		json_builder_end_array (builder);
	}

	/* include the protocol used */
	if (fwupd_release_get_protocol (rel) != NULL) {
		json_builder_set_member_name (builder, "Protocol");
		json_builder_add_string_value (builder, fwupd_release_get_protocol (rel));
	}

	/* set the error state of the report */
	json_builder_set_member_name (builder, "UpdateState");
	json_builder_add_int_value (builder, fwupd_device_get_update_state (dev));
	if (fwupd_device_get_update_error (dev) != NULL) {
		json_builder_set_member_name (builder, "UpdateError");
		json_builder_add_string_value (builder, fwupd_device_get_update_error (dev));
	}
	if (fwupd_release_get_update_message (rel) != NULL) {
		json_builder_set_member_name (builder, "UpdateMessage");
		json_builder_add_string_value (builder, fwupd_release_get_update_message (rel));
	}

	/* map back to the dev type on the LVFS */
	guids = fwupd_device_get_guids (dev);
	if (guids->len > 0) {
		json_builder_set_member_name (builder, "Guid");
		json_builder_begin_array (builder);
		for (guint i = 0; i < guids->len; i++) {
			const gchar *guid = g_ptr_array_index (guids, i);
			json_builder_add_string_value (builder, guid);
		}
		json_builder_end_array (builder);
	}

	json_builder_set_member_name (builder, "Plugin");
	json_builder_add_string_value (builder, fwupd_device_get_plugin (dev));

	/* report what we're trying to update *from* and *to* */
	json_builder_set_member_name (builder, "VersionOld");
	json_builder_add_string_value (builder, fwupd_device_get_version (dev));
	json_builder_set_member_name (builder, "VersionNew");
	json_builder_add_string_value (builder, fwupd_release_get_version (rel));

	/* to know the state of the dev we're trying to update */
	json_builder_set_member_name (builder, "Flags");
	json_builder_add_int_value (builder, fwupd_device_get_flags (dev));

	/* to know when the update tried to happen, and how soon after boot */
	json_builder_set_member_name (builder, "Created");
	json_builder_add_int_value (builder, fwupd_device_get_created (dev));
	json_builder_set_member_name (builder, "Modified");
	json_builder_add_int_value (builder, fwupd_device_get_modified (dev));

	/* add saved metadata to the report */
	json_builder_set_member_name (builder, "Metadata");
	json_builder_begin_object (builder);
	fwupd_build_history_report_json_metadata_device (builder, dev);
	json_builder_end_object (builder);
}

static gboolean
fwupd_build_history_report_json_metadata (JsonBuilder *builder, GError **error)
{
	g_autoptr(GHashTable) hash = NULL;
	struct {
		const gchar *key;
		const gchar *val;
	} distro_kv[] = {
		{ "ID",			"DistroId" },
		{ "VERSION_ID",		"DistroVersion" },
		{ "VARIANT_ID",		"DistroVariant" },
		{ NULL, NULL }
	};

	/* get all required os-release keys */
	hash = fwupd_get_os_release (error);
	if (hash == NULL)
		return FALSE;
	for (guint i = 0; distro_kv[i].key != NULL; i++) {
		const gchar *tmp = g_hash_table_lookup (hash, distro_kv[i].key);
		if (tmp != NULL) {
			json_builder_set_member_name (builder, distro_kv[i].val);
			json_builder_add_string_value (builder, tmp);
		}
	}
	return TRUE;
}

/**
 * fwupd_build_history_report_json:
 * @devices: (element-type FwupdDevice): devices
 * @error: A #GError or %NULL
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
fwupd_build_history_report_json (GPtrArray *devices, GError **error)
{
	gchar *data;
	g_autofree gchar *machine_id = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	/* get a hash that represents the machine */
	machine_id = fwupd_build_machine_id ("fwupd", error);
	if (machine_id == NULL)
		return NULL;

	/* create header */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "ReportVersion");
	json_builder_add_int_value (builder, 2);
	json_builder_set_member_name (builder, "MachineId");
	json_builder_add_string_value (builder, machine_id);

	/* this is system metadata not stored in the database */
	json_builder_set_member_name (builder, "Metadata");
	json_builder_begin_object (builder);
	if (!fwupd_build_history_report_json_metadata (builder, error))
		return NULL;
	json_builder_end_object (builder);

	/* add each device */
	json_builder_set_member_name (builder, "Reports");
	json_builder_begin_array (builder);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index (devices, i);
		json_builder_begin_object (builder);
		fwupd_build_history_report_json_device (builder, dev);
		json_builder_end_object (builder);
	}
	json_builder_end_array (builder);
	json_builder_end_object (builder);

	/* export as a string */
	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	data = json_generator_to_data (json_generator, NULL);
	if (data == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Failed to convert to JSON string");
		return NULL;
	}
	return data;
}

#define FWUPD_GUID_NAMESPACE_DEFAULT	"6ba7b810-9dad-11d1-80b4-00c04fd430c8"
#define FWUPD_GUID_NAMESPACE_MICROSOFT	"70ffd812-4c7f-4c7d-0000-000000000000"

typedef struct __attribute__((packed)) {
	guint32		a;
	guint16		b;
	guint16		c;
	guint16		d;
	guint8		e[6];
} fwupd_guid_native_t;

/**
 * fwupd_guid_to_string:
 * @guid: a #fwupd_guid_t to read
 * @flags: some %FwupdGuidFlags, e.g. %FWUPD_GUID_FLAG_MIXED_ENDIAN
 *
 * Returns a text GUID of mixed or BE endian for a packed buffer.
 *
 * Returns: A new GUID
 *
 * Since: 1.2.5
 **/
gchar *
fwupd_guid_to_string (const fwupd_guid_t *guid, FwupdGuidFlags flags)
{
	fwupd_guid_native_t gnat;

	g_return_val_if_fail (guid != NULL, NULL);

	/* copy to avoid issues with aligning */
	memcpy (&gnat, guid, sizeof(gnat));

	/* mixed is bizaar, but specified as the DCE encoding */
	if (flags & FWUPD_GUID_FLAG_MIXED_ENDIAN) {
		return g_strdup_printf ("%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x",
					GUINT32_FROM_LE(gnat.a),
					GUINT16_FROM_LE(gnat.b),
					GUINT16_FROM_LE(gnat.c),
					GUINT16_FROM_BE(gnat.d),
					gnat.e[0], gnat.e[1],
					gnat.e[2], gnat.e[3],
					gnat.e[4], gnat.e[5]);
	}
	return g_strdup_printf ("%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x",
				GUINT32_FROM_BE(gnat.a),
				GUINT16_FROM_BE(gnat.b),
				GUINT16_FROM_BE(gnat.c),
				GUINT16_FROM_BE(gnat.d),
				gnat.e[0], gnat.e[1],
				gnat.e[2], gnat.e[3],
				gnat.e[4], gnat.e[5]);
}

#if !GLIB_CHECK_VERSION(2,54,0)
static gboolean
str_has_sign (const gchar *str)
{
	return str[0] == '-' || str[0] == '+';
}

static gboolean
str_has_hex_prefix (const gchar *str)
{
	return str[0] == '0' && g_ascii_tolower (str[1]) == 'x';
}

static gboolean
g_ascii_string_to_unsigned (const gchar *str,
			    guint base,
			    guint64 min,
			    guint64 max,
			    guint64 *out_num,
			    GError **error)
{
	const gchar *end_ptr = NULL;
	gint saved_errno = 0;
	guint64 number;

	g_return_val_if_fail (str != NULL, FALSE);
	g_return_val_if_fail (base >= 2 && base <= 36, FALSE);
	g_return_val_if_fail (min <= max, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (str[0] == '\0') {
		g_set_error_literal (error,
				     G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
				     "Empty string is not a number");
		return FALSE;
	}

	errno = 0;
	number = g_ascii_strtoull (str, (gchar **)&end_ptr, base);
	saved_errno = errno;

	if (g_ascii_isspace (str[0]) || str_has_sign (str) ||
	    (base == 16 && str_has_hex_prefix (str)) ||
	    (saved_errno != 0 && saved_errno != ERANGE) ||
	    end_ptr == NULL ||
	    *end_ptr != '\0') {
		g_set_error (error,
			     G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "“%s” is not an unsigned number", str);
		return FALSE;
	}
	if (saved_errno == ERANGE || number < min || number > max) {
		g_autofree gchar *min_str = g_strdup_printf ("%" G_GUINT64_FORMAT, min);
		g_autofree gchar *max_str = g_strdup_printf ("%" G_GUINT64_FORMAT, max);
		g_set_error (error,
			     G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			     "Number “%s” is out of bounds [%s, %s]",
			     str, min_str, max_str);
		return FALSE;
	}
	if (out_num != NULL)
		*out_num = number;
	return TRUE;
}
#endif /* GLIB_CHECK_VERSION(2,54,0) */

/**
 * fwupd_guid_from_string:
 * @guidstr: (nullable): a GUID, e.g. `00112233-4455-6677-8899-aabbccddeeff`
 * @guid: a #fwupd_guid_t, or NULL to just check the GUID
 * @flags: some %FwupdGuidFlags, e.g. %FWUPD_GUID_FLAG_MIXED_ENDIAN
 * @error: A #GError or %NULL
 *
 * Converts a string GUID into its binary encoding. All string GUIDs are
 * formatted as big endian but on-disk can be encoded in different ways.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.2.5
 **/
gboolean
fwupd_guid_from_string (const gchar *guidstr,
			fwupd_guid_t *guid,
			FwupdGuidFlags flags,
			GError **error)
{
	fwupd_guid_native_t gu = { 0x0 };
	gboolean mixed_endian = flags & FWUPD_GUID_FLAG_MIXED_ENDIAN;
	guint64 tmp;
	g_auto(GStrv) split = NULL;

	g_return_val_if_fail (guidstr != NULL, FALSE);

	/* split into sections */
	if (strlen (guidstr) != 36) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "is not valid format");
		return FALSE;
	}
	split = g_strsplit (guidstr, "-", 5);
	if (g_strv_length (split) != 5) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "is not valid format, no dashes");
		return FALSE;
	}
	if (strlen (split[0]) != 8 && strlen (split[1]) != 4 &&
	    strlen (split[2]) != 4 && strlen (split[3]) != 4 &&
	    strlen (split[4]) != 12) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "is not valid format, not GUID");
		return FALSE;
	}

	/* parse */
	if (!g_ascii_string_to_unsigned (split[0], 16, 0, 0xffffffff, &tmp, error))
		return FALSE;
	gu.a = mixed_endian ? GUINT32_TO_LE(tmp) : GUINT32_TO_BE(tmp);
	if (!g_ascii_string_to_unsigned (split[1], 16, 0, 0xffff, &tmp, error))
		return FALSE;
	gu.b = mixed_endian ? GUINT16_TO_LE(tmp) : GUINT16_TO_BE(tmp);
	if (!g_ascii_string_to_unsigned (split[2], 16, 0, 0xffff, &tmp, error))
		return FALSE;
	gu.c = mixed_endian ? GUINT16_TO_LE(tmp) : GUINT16_TO_BE(tmp);
	if (!g_ascii_string_to_unsigned (split[3], 16, 0, 0xffff, &tmp, error))
		return FALSE;
	gu.d = GUINT16_TO_BE(tmp);
	for (guint i = 0; i < 6; i++) {
		gchar buffer[3] = { 0x0 };
		memcpy (buffer, split[4] + (i * 2), 2);
		if (!g_ascii_string_to_unsigned (buffer, 16, 0, 0xff, &tmp, error))
			return FALSE;
		gu.e[i] = tmp;
	}
	if (guid != NULL)
		memcpy (guid, &gu, sizeof(gu));

	/* success */
	return TRUE;
}

/**
 * fwupd_guid_hash_data:
 * @data: data to hash
 * @datasz: length of @data
 * @flags: some %FwupdGuidFlags, e.g. %FWUPD_GUID_FLAG_NAMESPACE_MICROSOFT
 *
 * Returns a GUID for some data. This uses a hash and so even small
 * differences in the @data will produce radically different return values.
 *
 * The implementation is taken from RFC4122, Section 4.1.3; specifically
 * using a type-5 SHA-1 hash.
 *
 * Returns: A new GUID, or %NULL for internal error
 *
 * Since: 1.2.5
 **/
gchar *
fwupd_guid_hash_data (const guint8 *data, gsize datasz, FwupdGuidFlags flags)
{
	const gchar *namespace_id = FWUPD_GUID_NAMESPACE_DEFAULT;
	gsize digestlen = 20;
	guint8 hash[20];
	fwupd_guid_t uu_namespace;
	fwupd_guid_t uu_new;
	g_autoptr(GChecksum) csum = NULL;

	g_return_val_if_fail (namespace_id != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (datasz != 0, NULL);

	/* old MS GUID */
	if (flags & FWUPD_GUID_FLAG_NAMESPACE_MICROSOFT)
		namespace_id = FWUPD_GUID_NAMESPACE_MICROSOFT;

	/* convert the namespace to binary: hardcoded BE, not @flags */
	if (!fwupd_guid_from_string (namespace_id, &uu_namespace, FWUPD_GUID_FLAG_NONE, NULL))
		return NULL;

	/* hash the namespace and then the string */
	csum = g_checksum_new (G_CHECKSUM_SHA1);
	g_checksum_update (csum, (guchar *) &uu_namespace, sizeof(uu_namespace));
	g_checksum_update (csum, (guchar *) data, (gssize) datasz);
	g_checksum_get_digest (csum, hash, &digestlen);

	/* copy most parts of the hash 1:1 */
	memcpy (uu_new, hash, sizeof(uu_new));

	/* set specific bits according to Section 4.1.3 */
	uu_new[6] = (guint8) ((uu_new[6] & 0x0f) | (5 << 4));
	uu_new[8] = (guint8) ((uu_new[8] & 0x3f) | 0x80);
	return fwupd_guid_to_string ((const fwupd_guid_t *) &uu_new, flags);
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
fwupd_guid_is_valid (const gchar *guid)
{
	if (guid == NULL)
		return FALSE;
	if (!fwupd_guid_from_string (guid, NULL, FWUPD_GUID_FLAG_NONE, NULL))
		return FALSE;
	if (g_strcmp0 (guid, "00000000-0000-0000-0000-000000000000") == 0)
		return FALSE;
	return TRUE;
}

/**
 * fwupd_guid_hash_string:
 * @str: A source string to use as a key
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
 * Returns: A new GUID, or %NULL if the string was invalid
 *
 * Since: 1.2.5
 **/
gchar *
fwupd_guid_hash_string (const gchar *str)
{
	if (str == NULL || str[0] == '\0')
		return NULL;
	return fwupd_guid_hash_data ((const guint8 *) str, strlen (str),
				     FWUPD_GUID_FLAG_NONE);
}
