/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuCommon"

#include <config.h>

#include <string.h>

#include "fwupd-error.h"

#include "fu-common-version.h"

#define FU_COMMON_VERSION_DECODE_BCD(val)	((((val) >> 4) & 0x0f) * 10 + ((val) & 0x0f))

/**
 * fu_common_version_format_from_string:
 * @str: A string, e.g. `quad`
 *
 * Converts text to a display version type.
 *
 * Returns: A #FuVersionFormat, e.g. %FU_VERSION_FORMAT_TRIPLET
 *
 * Since: 1.2.0
 **/
FuVersionFormat
fu_common_version_format_from_string (const gchar *str)
{
	if (g_strcmp0 (str, "triplet") == 0)
		return FU_VERSION_FORMAT_TRIPLET;
	if (g_strcmp0 (str, "quad") == 0)
		return FU_VERSION_FORMAT_QUAD;
	if (g_strcmp0 (str, "bcd") == 0)
		return FU_VERSION_FORMAT_BCD;
	if (g_strcmp0 (str, "plain") == 0)
		return FU_VERSION_FORMAT_PLAIN;
	if (g_strcmp0 (str, "intel-me") == 0)
		return FU_VERSION_FORMAT_INTEL_ME;
	return FU_VERSION_FORMAT_QUAD;
}

/**
 * fu_common_version_format_to_string:
 * @str: A #FuVersionFormat, e.g. %FU_VERSION_FORMAT_TRIPLET
 *
 * Converts a display version type to text.
 *
 * Returns: A string, e.g. `quad`, or %NULL if not known
 *
 * Since: 1.2.0
 **/
const gchar *
fu_common_version_format_to_string (FuVersionFormat kind)
{
	if (kind == FU_VERSION_FORMAT_TRIPLET)
		return "triplet";
	if (kind == FU_VERSION_FORMAT_QUAD)
		return "quad";
	if (kind == FU_VERSION_FORMAT_BCD)
		return "bcd";
	if (kind == FU_VERSION_FORMAT_PLAIN)
		return "plain";
	if (kind == FU_VERSION_FORMAT_INTEL_ME)
		return "intel-me";
	return NULL;
}

/**
 * fu_common_version_from_uint32:
 * @val: A uint32le version number
 * @kind: version kind used for formatting, e.g. %FU_VERSION_FORMAT_TRIPLET
 *
 * Returns a dotted decimal version string from a 32 bit number.
 *
 * Returns: A version number, e.g. "1.0.3", or %NULL if not supported
 *
 * Since: 1.2.0
 **/
gchar *
fu_common_version_from_uint32 (guint32 val, FuVersionFormat kind)
{
	if (kind == FU_VERSION_FORMAT_QUAD) {
		/* AA.BB.CC.DD */
		return g_strdup_printf ("%u.%u.%u.%u",
					(val >> 24) & 0xff,
					(val >> 16) & 0xff,
					(val >> 8) & 0xff,
					val & 0xff);
	}
	if (kind == FU_VERSION_FORMAT_TRIPLET) {
		/* AA.BB.CCDD */
		return g_strdup_printf ("%u.%u.%u",
					(val >> 24) & 0xff,
					(val >> 16) & 0xff,
					val & 0xffff);
	}
	if (kind == FU_VERSION_FORMAT_PAIR) {
		/* AABB.CCDD */
		return g_strdup_printf ("%u.%u",
					(val >> 16) & 0xffff,
					val & 0xffff);
	}
	if (kind == FU_VERSION_FORMAT_PLAIN) {
		/* AABBCCDD */
		return g_strdup_printf ("%" G_GUINT32_FORMAT, val);
	}
	if (kind == FU_VERSION_FORMAT_BCD) {
		/* AA.BB.CC.DD, but BCD */
		return g_strdup_printf ("%u.%u.%u.%u",
					FU_COMMON_VERSION_DECODE_BCD(val >> 24),
					FU_COMMON_VERSION_DECODE_BCD(val >> 16),
					FU_COMMON_VERSION_DECODE_BCD(val >> 8),
					FU_COMMON_VERSION_DECODE_BCD(val));
	}
	if (kind == FU_VERSION_FORMAT_INTEL_ME) {
		/* aaa+11.bbbbb.cccccccc.dddddddddddddddd */
		return g_strdup_printf ("%u.%u.%u.%u",
					((val >> 29) & 0x07) + 0x0b,
					 (val >> 24) & 0x1f,
					 (val >> 16) & 0xff,
					  val & 0xffff);
	}
	return NULL;
}

/**
 * fu_common_version_from_uint16:
 * @val: A uint16le version number
 * @kind: version kind used for formatting, e.g. %FU_VERSION_FORMAT_TRIPLET
 *
 * Returns a dotted decimal version string from a 16 bit number.
 *
 * Returns: A version number, e.g. "1.3", or %NULL if not supported
 *
 * Since: 1.2.0
 **/
gchar *
fu_common_version_from_uint16 (guint16 val, FuVersionFormat kind)
{
	if (kind == FU_VERSION_FORMAT_BCD) {
		return g_strdup_printf ("%i.%i",
					FU_COMMON_VERSION_DECODE_BCD(val >> 8),
					FU_COMMON_VERSION_DECODE_BCD(val));
	}
	if (kind == FU_VERSION_FORMAT_PAIR) {
		return g_strdup_printf ("%u.%u",
					(guint) (val >> 8) & 0xff,
					(guint) val & 0xff);
	}
	if (kind == FU_VERSION_FORMAT_PLAIN) {
		return g_strdup_printf ("%" G_GUINT16_FORMAT, val);
	}
	return NULL;
}

static gint
fu_common_vercmp_char (gchar chr1, gchar chr2)
{
	if (chr1 == chr2)
		return 0;
	if (chr1 == '~')
		return -1;
	if (chr2 == '~')
		return 1;
	return chr1 < chr2 ? -1 : 1;
}

static gint
fu_common_vercmp_chunk (const gchar *str1, const gchar *str2)
{
	guint i;

	/* trivial */
	if (g_strcmp0 (str1, str2) == 0)
		return 0;
	if (str1 == NULL)
		return 1;
	if (str2 == NULL)
		return -1;

	/* check each char of the chunk */
	for (i = 0; str1[i] != '\0' && str2[i] != '\0'; i++) {
		gint rc = fu_common_vercmp_char (str1[i], str2[i]);
		if (rc != 0)
			return rc;
	}
	return fu_common_vercmp_char (str1[i], str2[i]);
}

/**
 * fu_common_version_parse:
 * @version: A version number
 *
 * Returns a dotted decimal version string from a version string. The supported
 * formats are:
 *
 * - Dotted decimal, e.g. "1.2.3"
 * - Base 16, a hex number *with* a 0x prefix, e.g. "0x10203"
 * - Base 10, a string containing just [0-9], e.g. "66051"
 * - Date in YYYYMMDD format, e.g. 20150915
 *
 * Anything with a '.' or that doesn't match [0-9] or 0x[a-f,0-9] is considered
 * a string and returned without modification.
 *
 * Returns: A version number, e.g. "1.0.3"
 *
 * Since: 1.2.0
 */
gchar *
fu_common_version_parse (const gchar *version)
{
	const gchar *version_noprefix = version;
	gchar *endptr = NULL;
	guint64 tmp;
	guint base;
	guint i;

	/* already dotted decimal */
	if (g_strstr_len (version, -1, ".") != NULL)
		return g_strdup (version);

	/* is a date */
	if (g_str_has_prefix (version, "20") &&
	    strlen (version) == 8)
		return g_strdup (version);

	/* convert 0x prefixed strings to dotted decimal */
	if (g_str_has_prefix (version, "0x")) {
		version_noprefix += 2;
		base = 16;
	} else {
		/* for non-numeric content, just return the string */
		for (i = 0; version[i] != '\0'; i++) {
			if (!g_ascii_isdigit (version[i]))
				return g_strdup (version);
		}
		base = 10;
	}

	/* convert */
	tmp = g_ascii_strtoull (version_noprefix, &endptr, base);
	if (endptr != NULL && endptr[0] != '\0')
		return g_strdup (version);
	if (tmp == 0)
		return g_strdup (version);
	return fu_common_version_from_uint32 ((guint32) tmp, FU_VERSION_FORMAT_TRIPLET);
}

/**
 * fu_common_vercmp:
 * @version_a: the release version, e.g. 1.2.3
 * @version_b: the release version, e.g. 1.2.3.1
 *
 * Compares version numbers for sorting.
 *
 * Returns: -1 if a < b, +1 if a > b, 0 if they are equal, and %G_MAXINT on error
 *
 * Since: 0.3.5
 */
gint
fu_common_vercmp (const gchar *version_a, const gchar *version_b)
{
	guint longest_split;
	g_autofree gchar *str_a = NULL;
	g_autofree gchar *str_b = NULL;
	g_auto(GStrv) split_a = NULL;
	g_auto(GStrv) split_b = NULL;

	/* sanity check */
	if (version_a == NULL || version_b == NULL)
		return G_MAXINT;

	/* optimisation */
	if (g_strcmp0 (version_a, version_b) == 0)
		return 0;

	/* split into sections, and try to parse */
	str_a = fu_common_version_parse (version_a);
	str_b = fu_common_version_parse (version_b);
	split_a = g_strsplit (str_a, ".", -1);
	split_b = g_strsplit (str_b, ".", -1);
	longest_split = MAX (g_strv_length (split_a), g_strv_length (split_b));
	for (guint i = 0; i < longest_split; i++) {
		gchar *endptr_a = NULL;
		gchar *endptr_b = NULL;
		gint64 ver_a;
		gint64 ver_b;

		/* we lost or gained a dot */
		if (split_a[i] == NULL)
			return -1;
		if (split_b[i] == NULL)
			return 1;

		/* compare integers */
		ver_a = g_ascii_strtoll (split_a[i], &endptr_a, 10);
		ver_b = g_ascii_strtoll (split_b[i], &endptr_b, 10);
		if (ver_a < ver_b)
			return -1;
		if (ver_a > ver_b)
			return 1;

		/* compare strings */
		if ((endptr_a != NULL && endptr_a[0] != '\0') ||
		    (endptr_b != NULL && endptr_b[0] != '\0')) {
			gint rc = fu_common_vercmp_chunk (endptr_a, endptr_b);
			if (rc < 0)
				return -1;
			if (rc > 0)
				return 1;
		}
	}

	/* we really shouldn't get here */
	return 0;
}
