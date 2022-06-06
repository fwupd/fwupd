/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include <string.h>

#include "fu-common.h"
#include "fu-firmware-common.h"
#include "fu-mem.h"
#include "fu-string.h"

/**
 * fu_firmware_strparse_uint4_safe:
 * @data: destination buffer
 * @datasz: size of @data, typically the same as `strlen(data)`
 * @offset: offset in chars into @data to read
 * @value: (out) (nullable): parsed value
 * @error: (nullable): optional return location for an error
 *
 * Parses a base 16 number from a string of 1 character in length.
 * The returned @value will range from from 0 to 0xf.
 *
 * Returns: %TRUE if parsed, %FALSE otherwise
 *
 * Since: 1.5.6
 **/
gboolean
fu_firmware_strparse_uint4_safe(const gchar *data,
				gsize datasz,
				gsize offset,
				guint8 *value,
				GError **error)
{
	gchar buffer[2] = {'\0'};
	gchar *endptr = NULL;
	guint64 valuetmp;
	if (!fu_memcpy_safe((guint8 *)buffer,
			    sizeof(buffer),
			    0x0, /* dst */
			    (const guint8 *)data,
			    datasz,
			    offset, /* src */
			    sizeof(buffer) - 1,
			    error))
		return FALSE;
	valuetmp = g_ascii_strtoull(buffer, &endptr, 16);
	if (endptr - buffer != sizeof(buffer) - 1) {
		g_autofree gchar *str = fu_strsafe(buffer, sizeof(buffer));
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "cannot parse %s as hex",
			    str);
		return FALSE;
	}
	if (value != NULL)
		*value = (guint8)valuetmp;
	return TRUE;
}

/**
 * fu_firmware_strparse_uint8_safe:
 * @data: destination buffer
 * @datasz: size of @data, typically the same as `strlen(data)`
 * @offset: offset in chars into @data to read
 * @value: (out) (nullable): parsed value
 * @error: (nullable): optional return location for an error
 *
 * Parses a base 16 number from a string of 2 characters in length.
 * The returned @value will range from from 0 to 0xff.
 *
 * Returns: %TRUE if parsed, %FALSE otherwise
 *
 * Since: 1.5.6
 **/
gboolean
fu_firmware_strparse_uint8_safe(const gchar *data,
				gsize datasz,
				gsize offset,
				guint8 *value,
				GError **error)
{
	gchar buffer[3] = {'\0'};
	gchar *endptr = NULL;
	guint64 valuetmp;
	if (!fu_memcpy_safe((guint8 *)buffer,
			    sizeof(buffer),
			    0x0, /* dst */
			    (const guint8 *)data,
			    datasz,
			    offset, /* src */
			    sizeof(buffer) - 1,
			    error))
		return FALSE;
	valuetmp = g_ascii_strtoull(buffer, &endptr, 16);
	if (endptr - buffer != sizeof(buffer) - 1) {
		g_autofree gchar *str = fu_strsafe(buffer, sizeof(buffer));
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "cannot parse %s as hex",
			    str);
		return FALSE;
	}
	if (value != NULL)
		*value = (guint8)valuetmp;
	return TRUE;
}

/**
 * fu_firmware_strparse_uint16_safe:
 * @data: destination buffer
 * @datasz: size of @data, typically the same as `strlen(data)`
 * @offset: offset in chars into @data to read
 * @value: (out) (nullable): parsed value
 * @error: (nullable): optional return location for an error
 *
 * Parses a base 16 number from a string of 4 characters in length.
 * The returned @value will range from from 0 to 0xffff.
 *
 * Returns: %TRUE if parsed, %FALSE otherwise
 *
 * Since: 1.5.6
 **/
gboolean
fu_firmware_strparse_uint16_safe(const gchar *data,
				 gsize datasz,
				 gsize offset,
				 guint16 *value,
				 GError **error)
{
	gchar buffer[5] = {'\0'};
	gchar *endptr = NULL;
	guint64 valuetmp;
	if (!fu_memcpy_safe((guint8 *)buffer,
			    sizeof(buffer),
			    0x0, /* dst */
			    (const guint8 *)data,
			    datasz,
			    offset, /* src */
			    sizeof(buffer) - 1,
			    error))
		return FALSE;
	valuetmp = g_ascii_strtoull(buffer, &endptr, 16);
	if (endptr - buffer != sizeof(buffer) - 1) {
		g_autofree gchar *str = fu_strsafe(buffer, sizeof(buffer));
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "cannot parse %s as hex",
			    str);
		return FALSE;
	}
	if (value != NULL)
		*value = (guint16)valuetmp;
	return TRUE;
}

/**
 * fu_firmware_strparse_uint24_safe:
 * @data: destination buffer
 * @datasz: size of @data, typically the same as `strlen(data)`
 * @offset: offset in chars into @data to read
 * @value: (out) (nullable): parsed value
 * @error: (nullable): optional return location for an error
 *
 * Parses a base 16 number from a string of 6 characters in length.
 * The returned @value will range from from 0 to 0xffffff.
 *
 * Returns: %TRUE if parsed, %FALSE otherwise
 *
 * Since: 1.5.6
 **/
gboolean
fu_firmware_strparse_uint24_safe(const gchar *data,
				 gsize datasz,
				 gsize offset,
				 guint32 *value,
				 GError **error)
{
	gchar buffer[7] = {'\0'};
	gchar *endptr = NULL;
	guint64 valuetmp;
	if (!fu_memcpy_safe((guint8 *)buffer,
			    sizeof(buffer),
			    0x0, /* dst */
			    (const guint8 *)data,
			    datasz,
			    offset, /* src */
			    sizeof(buffer) - 1,
			    error))
		return FALSE;
	valuetmp = g_ascii_strtoull(buffer, &endptr, 16);
	if (endptr - buffer != sizeof(buffer) - 1) {
		g_autofree gchar *str = fu_strsafe(buffer, sizeof(buffer));
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "cannot parse %s as hex",
			    str);
		return FALSE;
	}
	if (value != NULL)
		*value = (guint16)valuetmp;
	return TRUE;
}

/**
 * fu_firmware_strparse_uint32_safe:
 * @data: destination buffer
 * @datasz: size of @data, typically the same as `strlen(data)`
 * @offset: offset in chars into @data to read
 * @value: (out) (nullable): parsed value
 * @error: (nullable): optional return location for an error
 *
 * Parses a base 16 number from a string of 8 characters in length.
 * The returned @value will range from from 0 to 0xffffffff.
 *
 * Returns: %TRUE if parsed, %FALSE otherwise
 *
 * Since: 1.5.6
 **/
gboolean
fu_firmware_strparse_uint32_safe(const gchar *data,
				 gsize datasz,
				 gsize offset,
				 guint32 *value,
				 GError **error)
{
	gchar buffer[9] = {'\0'};
	gchar *endptr = NULL;
	guint64 valuetmp;
	if (!fu_memcpy_safe((guint8 *)buffer,
			    sizeof(buffer),
			    0x0, /* dst */
			    (const guint8 *)data,
			    datasz,
			    offset, /* src */
			    sizeof(buffer) - 1,
			    error))
		return FALSE;
	valuetmp = g_ascii_strtoull(buffer, &endptr, 16);
	if (endptr - buffer != sizeof(buffer) - 1) {
		g_autofree gchar *str = fu_strsafe(buffer, sizeof(buffer));
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "cannot parse %s as hex",
			    str);
		return FALSE;
	}
	if (value != NULL)
		*value = (guint32)valuetmp;
	return TRUE;
}
