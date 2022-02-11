/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "fu-common-version.h"

#include <config.h>
#include <string.h>

#include "fwupd-enums.h"
#include "fwupd-error.h"

#define FU_COMMON_VERSION_DECODE_BCD(val) ((((val) >> 4) & 0x0f) * 10 + ((val)&0x0f))

/**
 * fu_common_version_from_uint64:
 * @val: a raw version number
 * @kind: version kind used for formatting, e.g. %FWUPD_VERSION_FORMAT_QUAD
 *
 * Returns a dotted decimal version string from a 64 bit number.
 *
 * Returns: a version number, e.g. `1.2.3.4`, or %NULL if not supported
 *
 * Since: 1.3.6
 **/
gchar *
fu_common_version_from_uint64(guint64 val, FwupdVersionFormat kind)
{
	if (kind == FWUPD_VERSION_FORMAT_QUAD) {
		/* AABB.CCDD.EEFF.GGHH */
		return g_strdup_printf("%" G_GUINT64_FORMAT "."
				       "%" G_GUINT64_FORMAT "."
				       "%" G_GUINT64_FORMAT "."
				       "%" G_GUINT64_FORMAT "",
				       (val >> 48) & 0xffff,
				       (val >> 32) & 0xffff,
				       (val >> 16) & 0xffff,
				       val & 0xffff);
	}
	if (kind == FWUPD_VERSION_FORMAT_PAIR) {
		/* AABBCCDD.EEFFGGHH */
		return g_strdup_printf("%" G_GUINT64_FORMAT ".%" G_GUINT64_FORMAT "",
				       (val >> 32) & 0xffffffff,
				       val & 0xffffffff);
	}
	if (kind == FWUPD_VERSION_FORMAT_NUMBER || kind == FWUPD_VERSION_FORMAT_PLAIN) {
		/* AABBCCDD */
		return g_strdup_printf("%" G_GUINT64_FORMAT, val);
	}
	if (kind == FWUPD_VERSION_FORMAT_HEX) {
		/* 0xAABBCCDDEEFFGGHH */
		return g_strdup_printf("0x%08x%08x",
				       (guint32)(val >> 32),
				       (guint32)(val & 0xffffffff));
	}
	g_critical("failed to convert version format %s: %" G_GUINT64_FORMAT "",
		   fwupd_version_format_to_string(kind),
		   val);
	return NULL;
}

/**
 * fu_common_version_from_uint32:
 * @val: a uint32le version number
 * @kind: version kind used for formatting, e.g. %FWUPD_VERSION_FORMAT_TRIPLET
 *
 * Returns a dotted decimal version string from a 32 bit number.
 *
 * Returns: a version number, e.g. `1.0.3`, or %NULL if not supported
 *
 * Since: 1.2.0
 **/
gchar *
fu_common_version_from_uint32(guint32 val, FwupdVersionFormat kind)
{
	if (kind == FWUPD_VERSION_FORMAT_QUAD) {
		/* AA.BB.CC.DD */
		return g_strdup_printf("%u.%u.%u.%u",
				       (val >> 24) & 0xff,
				       (val >> 16) & 0xff,
				       (val >> 8) & 0xff,
				       val & 0xff);
	}
	if (kind == FWUPD_VERSION_FORMAT_TRIPLET) {
		/* AA.BB.CCDD */
		return g_strdup_printf("%u.%u.%u",
				       (val >> 24) & 0xff,
				       (val >> 16) & 0xff,
				       val & 0xffff);
	}
	if (kind == FWUPD_VERSION_FORMAT_PAIR) {
		/* AABB.CCDD */
		return g_strdup_printf("%u.%u", (val >> 16) & 0xffff, val & 0xffff);
	}
	if (kind == FWUPD_VERSION_FORMAT_NUMBER || kind == FWUPD_VERSION_FORMAT_PLAIN) {
		/* AABBCCDD */
		return g_strdup_printf("%" G_GUINT32_FORMAT, val);
	}
	if (kind == FWUPD_VERSION_FORMAT_BCD) {
		/* AA.BB.CC.DD, but BCD */
		return g_strdup_printf("%u.%u.%u.%u",
				       FU_COMMON_VERSION_DECODE_BCD(val >> 24),
				       FU_COMMON_VERSION_DECODE_BCD(val >> 16),
				       FU_COMMON_VERSION_DECODE_BCD(val >> 8),
				       FU_COMMON_VERSION_DECODE_BCD(val));
	}
	if (kind == FWUPD_VERSION_FORMAT_INTEL_ME) {
		/* aaa+11.bbbbb.cccccccc.dddddddddddddddd */
		return g_strdup_printf("%u.%u.%u.%u",
				       ((val >> 29) & 0x07) + 0x0b,
				       (val >> 24) & 0x1f,
				       (val >> 16) & 0xff,
				       val & 0xffff);
	}
	if (kind == FWUPD_VERSION_FORMAT_INTEL_ME2) {
		/* A.B.CC.DDDD */
		return g_strdup_printf("%u.%u.%u.%u",
				       (val >> 28) & 0x0f,
				       (val >> 24) & 0x0f,
				       (val >> 16) & 0xff,
				       val & 0xffff);
	}
	if (kind == FWUPD_VERSION_FORMAT_SURFACE_LEGACY) {
		/* 10b.12b.10b */
		return g_strdup_printf("%u.%u.%u",
				       (val >> 22) & 0x3ff,
				       (val >> 10) & 0xfff,
				       val & 0x3ff);
	}
	if (kind == FWUPD_VERSION_FORMAT_SURFACE) {
		/* 8b.16b.8b */
		return g_strdup_printf("%u.%u.%u",
				       (val >> 24) & 0xff,
				       (val >> 8) & 0xffff,
				       val & 0xff);
	}
	if (kind == FWUPD_VERSION_FORMAT_DELL_BIOS) {
		/* BB.CC.DD */
		return g_strdup_printf("%u.%u.%u",
				       (val >> 16) & 0xff,
				       (val >> 8) & 0xff,
				       val & 0xff);
	}
	if (kind == FWUPD_VERSION_FORMAT_HEX) {
		/* 0xAABBCCDD */
		return g_strdup_printf("0x%08x", val);
	}
	g_critical("failed to convert version format %s: %u",
		   fwupd_version_format_to_string(kind),
		   val);
	return NULL;
}

/**
 * fu_common_version_from_uint16:
 * @val: a uint16le version number
 * @kind: version kind used for formatting, e.g. %FWUPD_VERSION_FORMAT_TRIPLET
 *
 * Returns a dotted decimal version string from a 16 bit number.
 *
 * Returns: a version number, e.g. `1.3`, or %NULL if not supported
 *
 * Since: 1.2.0
 **/
gchar *
fu_common_version_from_uint16(guint16 val, FwupdVersionFormat kind)
{
	if (kind == FWUPD_VERSION_FORMAT_BCD) {
		return g_strdup_printf("%i.%i",
				       FU_COMMON_VERSION_DECODE_BCD(val >> 8),
				       FU_COMMON_VERSION_DECODE_BCD(val));
	}
	if (kind == FWUPD_VERSION_FORMAT_PAIR) {
		return g_strdup_printf("%u.%u", (guint)(val >> 8) & 0xff, (guint)val & 0xff);
	}
	if (kind == FWUPD_VERSION_FORMAT_NUMBER || kind == FWUPD_VERSION_FORMAT_PLAIN) {
		return g_strdup_printf("%" G_GUINT16_FORMAT, val);
	}
	if (kind == FWUPD_VERSION_FORMAT_HEX) {
		/* 0xAABB */
		return g_strdup_printf("0x%04x", val);
	}
	g_critical("failed to convert version format %s: %u",
		   fwupd_version_format_to_string(kind),
		   val);
	return NULL;
}

static gint
fu_common_vercmp_char(gchar chr1, gchar chr2)
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
fu_common_vercmp_chunk(const gchar *str1, const gchar *str2)
{
	guint i;

	/* trivial */
	if (g_strcmp0(str1, str2) == 0)
		return 0;
	if (str1 == NULL)
		return 1;
	if (str2 == NULL)
		return -1;

	/* check each char of the chunk */
	for (i = 0; str1[i] != '\0' && str2[i] != '\0'; i++) {
		gint rc = fu_common_vercmp_char(str1[i], str2[i]);
		if (rc != 0)
			return rc;
	}
	return fu_common_vercmp_char(str1[i], str2[i]);
}

static gboolean
_g_ascii_is_digits(const gchar *str)
{
	g_return_val_if_fail(str != NULL, FALSE);
	for (gsize i = 0; str[i] != '\0'; i++) {
		if (!g_ascii_isdigit(str[i]))
			return FALSE;
	}
	return TRUE;
}

static guint
fwupd_version_format_number_sections(FwupdVersionFormat fmt)
{
	if (fmt == FWUPD_VERSION_FORMAT_PLAIN || fmt == FWUPD_VERSION_FORMAT_NUMBER ||
	    fmt == FWUPD_VERSION_FORMAT_HEX)
		return 1;
	if (fmt == FWUPD_VERSION_FORMAT_PAIR || fmt == FWUPD_VERSION_FORMAT_BCD)
		return 2;
	if (fmt == FWUPD_VERSION_FORMAT_TRIPLET || fmt == FWUPD_VERSION_FORMAT_SURFACE_LEGACY ||
	    fmt == FWUPD_VERSION_FORMAT_SURFACE || fmt == FWUPD_VERSION_FORMAT_DELL_BIOS)
		return 3;
	if (fmt == FWUPD_VERSION_FORMAT_QUAD || fmt == FWUPD_VERSION_FORMAT_INTEL_ME ||
	    fmt == FWUPD_VERSION_FORMAT_INTEL_ME2)
		return 4;
	return 0;
}

/**
 * fu_common_version_ensure_semver_full:
 * @version: (nullable): a version number, e.g. ` V1.2.3 `
 * @fmt: a version format, e.g. %FWUPD_VERSION_FORMAT_TRIPLET
 *
 * Builds a semver from the possibly crazy version number. Depending on the @semver value
 * the string will be split and a string in the correct format will be returned.
 *
 * Returns: a version number, e.g. `1.2.3`, or %NULL if the version was not valid
 *
 * Since: 1.7.6
 */
gchar *
fu_common_version_ensure_semver_full(const gchar *version, FwupdVersionFormat fmt)
{
	guint sections_actual;
	guint sections_expected = fwupd_version_format_number_sections(fmt);
	g_autofree gchar *tmp = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* split into all sections */
	tmp = fu_common_version_ensure_semver(version);
	if (tmp == NULL)
		return NULL;
	if (fmt == FWUPD_VERSION_FORMAT_UNKNOWN)
		return g_steal_pointer(&tmp);
	split = g_strsplit(tmp, ".", -1);
	sections_actual = g_strv_length(split);

	/* add zero sections as required */
	if (sections_actual < sections_expected) {
		for (guint i = 0; i < sections_expected - sections_actual; i++) {
			if (str->len > 0)
				g_string_append(str, ".");
			g_string_append(str, "0");
		}
	}

	/* only add enough sections for the format */
	for (guint i = 0; i < sections_actual && i < sections_expected; i++) {
		if (str->len > 0)
			g_string_append(str, ".");
		g_string_append(str, split[i]);
	}

	/* success */
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/**
 * fu_common_version_ensure_semver:
 * @version: (nullable): a version number, e.g. ` V1.2.3 `
 *
 * Builds a semver from the possibly crazy version number.
 *
 * Returns: a version number, e.g. `1.2.3`, or %NULL if the version was not valid
 *
 * Since: 1.2.9
 */
gchar *
fu_common_version_ensure_semver(const gchar *version)
{
	gboolean dot_valid = FALSE;
	guint digit_cnt = 0;
	g_autoptr(GString) version_safe = g_string_new(NULL);

	/* invalid */
	if (version == NULL)
		return NULL;

	/* hex prefix */
	if (g_str_has_prefix(version, "0x")) {
		return fu_common_version_parse_from_format(version, FWUPD_VERSION_FORMAT_TRIPLET);
	}

	/* make sane */
	for (guint i = 0; version[i] != '\0'; i++) {
		if (g_ascii_isdigit(version[i])) {
			g_string_append_c(version_safe, version[i]);
			digit_cnt++;
			dot_valid = TRUE;
			continue;
		}
		if (version[i] == '-' || version[i] == '~') {
			g_string_append_c(version_safe, '.');
			dot_valid = FALSE;
			continue;
		}
		if (version[i] == '.' && dot_valid && version[i + 1] != '\0') {
			g_string_append_c(version_safe, version[i]);
			dot_valid = FALSE;
			continue;
		}
	}

	/* remove any trailing dot */
	if (version_safe->len > 0 && version_safe->str[version_safe->len - 1] == '.')
		g_string_truncate(version_safe, version_safe->len - 1);

	/* found no digits */
	if (digit_cnt == 0)
		return NULL;
	return g_string_free(g_steal_pointer(&version_safe), FALSE);
}

/**
 * fu_common_version_parse_from_format
 * @version: (nullable): a version number
 * @fmt: a version format, e.g. %FWUPD_VERSION_FORMAT_TRIPLET
 *
 * Returns a dotted decimal version string from a version string using @fmt.
 * The supported formats are:
 *
 * - Dotted decimal, e.g. `1.2.3`
 * - Base 16, a hex number *with* a 0x prefix, e.g. `0x10203`
 * - Base 10, a string containing just [0-9], e.g. `66051`
 * - Date in YYYYMMDD format, e.g. `20150915`
 *
 * Anything with a `.` or that doesn't match `[0-9]` or `0x[a-f,0-9]` is considered
 * a string and returned without modification.
 *
 * Returns: a version number, e.g. `1.0.3`, or %NULL on error
 *
 * Since: 1.3.3
 */
gchar *
fu_common_version_parse_from_format(const gchar *version, FwupdVersionFormat fmt)
{
	const gchar *version_noprefix = version;
	gchar *endptr = NULL;
	guint64 tmp;
	guint base;

	/* sanity check */
	if (version == NULL)
		return NULL;

	/* already dotted decimal */
	if (g_strstr_len(version, -1, ".") != NULL)
		return g_strdup(version);

	/* is a date */
	if (g_str_has_prefix(version, "20") && strlen(version) == 8)
		return g_strdup(version);

	/* convert 0x prefixed strings to dotted decimal */
	if (g_str_has_prefix(version, "0x")) {
		version_noprefix += 2;
		base = 16;
	} else {
		/* for non-numeric content, just return the string */
		if (!_g_ascii_is_digits(version))
			return g_strdup(version);
		base = 10;
	}

	/* convert */
	tmp = g_ascii_strtoull(version_noprefix, &endptr, base);
	if (endptr != NULL && endptr[0] != '\0')
		return g_strdup(version);
	if (tmp == 0)
		return g_strdup(version);
	return fu_common_version_from_uint32((guint32)tmp, fmt);
}

/**
 * fu_common_version_guess_format:
 * @version: (nullable): a version number, e.g. `1.2.3`
 *
 * Guesses the version format from the version number. This is only a heuristic
 * and plugins and components should explicitly set the version format whenever
 * possible.
 *
 * If the version format cannot be guessed with any degree of accuracy, the
 * %FWUPD_VERSION_FORMAT_UNKNOWN constant is returned.
 *
 * Returns: a version format, e.g. %FWUPD_VERSION_FORMAT_QUAD
 *
 * Since: 1.2.0
 */
FwupdVersionFormat
fu_common_version_guess_format(const gchar *version)
{
	guint sz;
	g_auto(GStrv) split = NULL;

	/* nothing to use */
	if (version == NULL || version[0] == '\0')
		return FWUPD_VERSION_FORMAT_UNKNOWN;

	/* no dots, assume just text */
	split = g_strsplit(version, ".", -1);
	sz = g_strv_length(split);
	if (sz == 1) {
		if (g_str_has_prefix(version, "0x") || _g_ascii_is_digits(version))
			return FWUPD_VERSION_FORMAT_NUMBER;
		return FWUPD_VERSION_FORMAT_PLAIN;
	}

	/* check for only-digit semver version */
	for (guint i = 0; split[i] != NULL; i++) {
		/* check sections are plain numbers */
		if (!_g_ascii_is_digits(split[i]))
			return FWUPD_VERSION_FORMAT_PLAIN;
	}

	/* the most common formats */
	if (sz == 2)
		return FWUPD_VERSION_FORMAT_PAIR;
	if (sz == 3)
		return FWUPD_VERSION_FORMAT_TRIPLET;
	if (sz == 4)
		return FWUPD_VERSION_FORMAT_QUAD;

	/* unknown! */
	return FWUPD_VERSION_FORMAT_UNKNOWN;
}

static FwupdVersionFormat
fu_common_version_convert_base(FwupdVersionFormat fmt)
{
	if (fmt == FWUPD_VERSION_FORMAT_INTEL_ME || fmt == FWUPD_VERSION_FORMAT_INTEL_ME2)
		return FWUPD_VERSION_FORMAT_QUAD;
	if (fmt == FWUPD_VERSION_FORMAT_DELL_BIOS)
		return FWUPD_VERSION_FORMAT_TRIPLET;
	if (fmt == FWUPD_VERSION_FORMAT_BCD)
		return FWUPD_VERSION_FORMAT_PAIR;
	if (fmt == FWUPD_VERSION_FORMAT_HEX)
		return FWUPD_VERSION_FORMAT_NUMBER;
	return fmt;
}

/**
 * fu_common_version_verify_format:
 * @version: (not nullable): a string, e.g. `0x1234`
 * @fmt: a version format
 * @error: (nullable): optional return location for an error
 *
 * Verifies if a version matches the input format.
 *
 * Returns: TRUE or FALSE
 *
 * Since: 1.2.9
 **/
gboolean
fu_common_version_verify_format(const gchar *version, FwupdVersionFormat fmt, GError **error)
{
	FwupdVersionFormat fmt_base = fu_common_version_convert_base(fmt);
	FwupdVersionFormat fmt_guess;

	g_return_val_if_fail(version != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* don't touch */
	if (fmt == FWUPD_VERSION_FORMAT_PLAIN)
		return TRUE;

	/* nothing we can check for */
	if (fmt == FWUPD_VERSION_FORMAT_UNKNOWN)
		return TRUE;

	/* check the base format */
	fmt_guess = fu_common_version_guess_format(version);
	if (fmt_guess != fmt_base) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "%s is not a valid %s (guessed %s)",
			    version,
			    fwupd_version_format_to_string(fmt),
			    fwupd_version_format_to_string(fmt_guess));
		return FALSE;
	}
	return TRUE;
}

static gint
fu_common_vercmp_safe(const gchar *version_a, const gchar *version_b)
{
	guint longest_split;
	g_auto(GStrv) split_a = NULL;
	g_auto(GStrv) split_b = NULL;

	/* sanity check */
	if (version_a == NULL || version_b == NULL)
		return G_MAXINT;

	/* optimization */
	if (g_strcmp0(version_a, version_b) == 0)
		return 0;

	/* split into sections, and try to parse */
	split_a = g_strsplit(version_a, ".", -1);
	split_b = g_strsplit(version_b, ".", -1);
	longest_split = MAX(g_strv_length(split_a), g_strv_length(split_b));
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
		ver_a = g_ascii_strtoll(split_a[i], &endptr_a, 10);
		ver_b = g_ascii_strtoll(split_b[i], &endptr_b, 10);
		if (ver_a < ver_b)
			return -1;
		if (ver_a > ver_b)
			return 1;

		/* compare strings */
		if ((endptr_a != NULL && endptr_a[0] != '\0') ||
		    (endptr_b != NULL && endptr_b[0] != '\0')) {
			gint rc = fu_common_vercmp_chunk(endptr_a, endptr_b);
			if (rc < 0)
				return -1;
			if (rc > 0)
				return 1;
		}
	}

	/* we really shouldn't get here */
	return 0;
}

/**
 * fu_common_vercmp_full:
 * @version_a: (nullable): the semver release version, e.g. `1.2.3`
 * @version_b: (nullable): the semver release version, e.g. `1.2.3.1`
 * @fmt: a version format, e.g. %FWUPD_VERSION_FORMAT_PLAIN
 *
 * Compares version numbers for sorting taking into account the version format
 * if required.
 *
 * Returns: -1 if a < b, +1 if a > b, 0 if they are equal, and %G_MAXINT on error
 *
 * Since: 1.3.9
 */
gint
fu_common_vercmp_full(const gchar *version_a, const gchar *version_b, FwupdVersionFormat fmt)
{
	if (fmt == FWUPD_VERSION_FORMAT_PLAIN)
		return g_strcmp0(version_a, version_b);
	if (fmt == FWUPD_VERSION_FORMAT_HEX) {
		g_autofree gchar *hex_a = NULL;
		g_autofree gchar *hex_b = NULL;
		hex_a = fu_common_version_parse_from_format(version_a, fmt);
		hex_b = fu_common_version_parse_from_format(version_b, fmt);
		return fu_common_vercmp_safe(hex_a, hex_b);
	}
	return fu_common_vercmp_safe(version_a, version_b);
}
