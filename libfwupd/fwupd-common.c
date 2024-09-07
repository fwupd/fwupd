/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
	g_return_val_if_fail(checksum != NULL, G_CHECKSUM_SHA1);
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

#ifdef HOST_MACHINE_SYSTEM_DARWIN
static GHashTable *
fwupd_get_os_release_darwin(GError **error)
{
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
}
#endif

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
#ifdef HOST_MACHINE_SYSTEM_DARWIN
	if (filename2 == NULL)
		return fwupd_get_os_release_darwin(error);
#endif
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
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fwupd_get_os_release_full(NULL, error);
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

#define FWUPD_GUID_NAMESPACE_DEFAULT   "6ba7b810-9dad-11d1-80b4-00c04fd430c8"
#define FWUPD_GUID_NAMESPACE_MICROSOFT "70ffd812-4c7f-4c7d-0000-000000000000"

typedef struct __attribute__((packed)) { /* nocheck:blocked */
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
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "GUID is not valid format");
		return FALSE;
	}
	split = g_strsplit(guidstr, "-", 5);
	if (g_strv_length(split) != 5) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "GUID is not valid format, no dashes");
		return FALSE;
	}
	if (strlen(split[0]) != 8 || strlen(split[1]) != 4 || strlen(split[2]) != 4 ||
	    strlen(split[3]) != 4 || strlen(split[4]) != 12) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
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

/**
 * fwupd_unix_output_stream_from_fn: (skip):
 **/
GUnixOutputStream *
fwupd_unix_output_stream_from_fn(const gchar *fn, GError **error)
{
	gint fd = g_open(fn, O_RDWR | O_CREAT, S_IRWXU);
	if (fd < 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "failed to open %s", fn);
		return NULL;
	}
	return G_UNIX_OUTPUT_STREAM(g_unix_output_stream_new(fd, TRUE));
}
#endif
