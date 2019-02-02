/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuCommon"

#include <config.h>

#include <string.h>
#include <uuid.h>

#include "fwupd-error.h"

#include "fu-common-guid.h"

/**
 * fu_common_guid_from_data:
 * @namespace_id: A namespace ID, e.g. "6ba7b810-9dad-11d1-80b4-00c04fd430c8"
 * @data: data to hash
 * @data_len: length of @data
 * @error: A #GError or %NULL
 *
 * Returns a GUID for some data. This uses a hash and so even small
 * differences in the @data will produce radically different return values.
 *
 * The implementation is taken from RFC4122, Section 4.1.3; specifically
 * using a type-5 SHA-1 hash.
 *
 * Returns: A new GUID, or %NULL if the namespace_id was invalid
 *
 * Since: 1.2.0
 **/
gchar *
fu_common_guid_from_data (const gchar *namespace_id,
			  const guint8 *data,
			  gsize data_len,
			  GError **error)
{
	gchar guid_new[37]; /* 36 plus NUL */
	gsize digestlen = 20;
	guint8 hash[20];
	gint rc;
	uuid_t uu_namespace;
	uuid_t uu_new;
	g_autoptr(GChecksum) csum = NULL;

	g_return_val_if_fail (namespace_id != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (data_len != 0, NULL);

	/* convert the namespace to binary */
	rc = uuid_parse (namespace_id, uu_namespace);
	if (rc != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "namespace '%s' is invalid",
			     namespace_id);
		return NULL;
	}

	/* hash the namespace and then the string */
	csum = g_checksum_new (G_CHECKSUM_SHA1);
	g_checksum_update (csum, (guchar *) uu_namespace, 16);
	g_checksum_update (csum, (guchar *) data, (gssize) data_len);
	g_checksum_get_digest (csum, hash, &digestlen);

	/* copy most parts of the hash 1:1 */
	memcpy (uu_new, hash, 16);

	/* set specific bits according to Section 4.1.3 */
	uu_new[6] = (guint8) ((uu_new[6] & 0x0f) | (5 << 4));
	uu_new[8] = (guint8) ((uu_new[8] & 0x3f) | 0x80);

	/* return as a string */
	uuid_unparse (uu_new, guid_new);
	return g_strdup (guid_new);
}

/**
 * fu_common_guid_is_valid:
 * @guid: string to check
 *
 * Checks the source string is a valid string GUID descriptor.
 *
 * Returns: %TRUE if @guid was a valid GUID, %FALSE otherwise
 *
 * Since: 1.2.0
 **/
gboolean
fu_common_guid_is_valid (const gchar *guid)
{
	gint rc;
	uuid_t uu;
	if (guid == NULL)
		return FALSE;
	if (strlen (guid) != 36)
		return FALSE;
	rc = uuid_parse (guid, uu);
	if (uuid_is_null (uu))
		return FALSE;
	return rc == 0;
}

/**
 * fu_common_guid_from_string:
 * @str: A source string to use as a key
 *
 * Returns a GUID for a given string. This uses a hash and so even small
 * differences in the @str will produce radically different return values.
 *
 * The implementation is taken from RFC4122, Section 4.1.3; specifically
 * using a type-5 SHA-1 hash with a DNS namespace.
 * The same result can be obtained with this simple python program:
 *
 *    #!/usr/bin/python
 *    import uuid
 *    print uuid.uuid5(uuid.NAMESPACE_DNS, 'python.org')
 *
 * Returns: A new GUID, or %NULL if the string was invalid
 *
 * Since: 1.2.0
 **/
gchar *
fu_common_guid_from_string (const gchar *str)
{
	if (str == NULL)
		return NULL;
	return fu_common_guid_from_data ("6ba7b810-9dad-11d1-80b4-00c04fd430c8",
					(const guint8 *) str, strlen (str), NULL);
}

/**
 * fu_common_guid_is_plausible:
 * @buf: a buffer of data
 *
 * Checks whether a chunk of memory looks like it could be a GUID.
 *
 * Returns: TRUE if it looks like a GUID, FALSE if not
 *
 * Since: 1.2.5
 **/
gboolean
fu_common_guid_is_plausible (const guint8 *buf)
{
	guint guint_sum = 0;

	for (guint i = 0; i < 16; i++)
		guint_sum += buf[i];
	if (guint_sum == 0x00)
		return FALSE;
	if (guint_sum < 0xff)
		return FALSE;
	return TRUE;
}
