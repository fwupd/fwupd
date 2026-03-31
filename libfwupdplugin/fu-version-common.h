/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>
#include <gio/gio.h>

/**
 * fu_version_compare:
 * @version_a: (nullable): the semver release version, e.g. `1.2.3`
 * @version_b: (nullable): the semver release version, e.g. `1.2.3.1`
 * @fmt: a version format, e.g. %FWUPD_VERSION_FORMAT_PLAIN
 *
 * Compares version numbers for sorting taking into account the version format
 * if required.
 *
 * Returns: -1 if a < b, +1 if a > b, 0 if they are equal, and %G_MAXINT on error
 *
 * Since: 1.8.2
 */
gint
fu_version_compare(const gchar *version_a, const gchar *version_b, FwupdVersionFormat fmt);
/**
 * fu_version_from_uint64:
 * @val: a raw version number
 * @kind: version kind used for formatting, e.g. %FWUPD_VERSION_FORMAT_QUAD
 *
 * Returns a dotted decimal version string from a 64 bit number.
 *
 * Returns: a version number, e.g. `1.2.3.4`, or %NULL if not supported
 *
 * Since: 1.8.2
 **/
gchar *
fu_version_from_uint64(guint64 val, FwupdVersionFormat kind);
/**
 * fu_version_from_uint32:
 * @val: a uint32le version number
 * @kind: version kind used for formatting, e.g. %FWUPD_VERSION_FORMAT_TRIPLET
 *
 * Returns a dotted decimal version string from a 32 bit number.
 *
 * Returns: a version number, e.g. `1.0.3`, or %NULL if not supported
 *
 * Since: 1.8.2
 **/
gchar *
fu_version_from_uint32(guint32 val, FwupdVersionFormat kind);
/**
 * fu_version_from_uint32_hex:
 * @val: a uint32le version number
 * @kind: version kind used for formatting, e.g. %FWUPD_VERSION_FORMAT_TRIPLET
 *
 * Returns a dotted decimal hex string from a 32 bit number.
 *
 * Returns: a version number, e.g. `1a.0.d3`, or %NULL if not supported
 *
 * Since: 2.0.0
 **/
gchar *
fu_version_from_uint32_hex(guint32 val, FwupdVersionFormat kind);
/**
 * fu_version_from_uint24:
 * @val: a uint24le version number
 * @kind: version kind used for formatting, e.g. %FWUPD_VERSION_FORMAT_TRIPLET
 *
 * Returns a dotted decimal version string from a 24 bit number.
 *
 * Returns: a version number, e.g. `1.0.3`, or %NULL if not supported
 *
 * Since: 1.8.9
 **/
gchar *
fu_version_from_uint24(guint32 val, FwupdVersionFormat kind);
/**
 * fu_version_from_uint16:
 * @val: a uint16le version number
 * @kind: version kind used for formatting, e.g. %FWUPD_VERSION_FORMAT_TRIPLET
 *
 * Returns a dotted decimal version string from a 16 bit number.
 *
 * Returns: a version number, e.g. `1.3`, or %NULL if not supported
 *
 * Since: 1.8.2
 **/
gchar *
fu_version_from_uint16(guint16 val, FwupdVersionFormat kind);
/**
 * fu_version_from_uint16_hex:
 * @val: a uint16le version number
 * @kind: version kind used for formatting, e.g. %FWUPD_VERSION_FORMAT_TRIPLET
 *
 * Returns a dotted hex version string from a 16 bit number.
 *
 * Returns: a version number, e.g. `1a.f3`, or %NULL if not supported
 *
 * Since: 2.0.0
 **/
gchar *
fu_version_from_uint16_hex(guint16 val, FwupdVersionFormat kind);
/**
 * fu_version_parse_from_format:
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
 * Since: 1.8.2
 */
gchar *
fu_version_parse_from_format(const gchar *version, FwupdVersionFormat fmt);
/**
 * fu_version_ensure_semver:
 * @version: (nullable): a version number, e.g. ` V1.2.3 `
 * @fmt: a version format, e.g. %FWUPD_VERSION_FORMAT_TRIPLET
 *
 * Builds a semver from the possibly crazy version number. Depending on the @semver value
 * the string will be split and a string in the correct format will be returned.
 *
 * Returns: a version number, e.g. `1.2.3`, or %NULL if the version was not valid
 *
 * Since: 1.8.2
 */
gchar *
fu_version_ensure_semver(const gchar *version, FwupdVersionFormat fmt) G_GNUC_NON_NULL(1);
/**
 * fu_version_guess_format:
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
 * Since: 1.8.2
 */
FwupdVersionFormat
fu_version_guess_format(const gchar *version);
/**
 * fu_version_verify_format:
 * @version: (not nullable): a string, e.g. `0x1234`
 * @fmt: a version format
 * @error: (nullable): optional return location for an error
 *
 * Verifies if a version matches the input format.
 *
 * Returns: TRUE or FALSE
 *
 * Since: 1.8.2
 **/
gboolean
fu_version_verify_format(const gchar *version,
			 FwupdVersionFormat fmt,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
