/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuFirmware"

#include <config.h>

#include <string.h>

#include "fu-common.h"
#include "fu-firmware-common.h"

/**
 * fu_firmware_strparse_uint4:
 * @data: a string
 *
 * Parses a base 16 number from a string.
 *
 * The string MUST be at least 1 byte long as this function cannot check the
 * length of @data. Checking the size must be done in the caller.
 *
 * Return value: A parsed value, or 0 for error
 *
 * Since: 1.3.1
 **/
guint8
fu_firmware_strparse_uint4 (const gchar *data)
{
	gchar buffer[2];
	memcpy (buffer, data, 1);
	buffer[1] = '\0';
	return (guint8) g_ascii_strtoull (buffer, NULL, 16);
}

/**
 * fu_firmware_strparse_uint8:
 * @data: a string
 *
 * Parses a base 16 number from a string.
 *
 * The string MUST be at least 2 bytes long as this function cannot check the
 * length of @data. Checking the size must be done in the caller.
 *
 * Return value: A parsed value, or 0 for error
 *
 * Since: 1.3.1
 **/
guint8
fu_firmware_strparse_uint8 (const gchar *data)
{
	gchar buffer[3];
	memcpy (buffer, data, 2);
	buffer[2] = '\0';
	return (guint8) g_ascii_strtoull (buffer, NULL, 16);
}

/**
 * fu_firmware_strparse_uint16:
 * @data: a string
 *
 * Parses a base 16 number from a string.
 *
 * The string MUST be at least 4 bytes long as this function cannot check the
 * length of @data. Checking the size must be done in the caller.
 *
 * Return value: A parsed value, or 0 for error
 *
 * Since: 1.3.1
 **/
guint16
fu_firmware_strparse_uint16 (const gchar *data)
{
	gchar buffer[5];
	memcpy (buffer, data, 4);
	buffer[4] = '\0';
	return (guint16) g_ascii_strtoull (buffer, NULL, 16);
}

/**
 * fu_firmware_strparse_uint24:
 * @data: a string
 *
 * Parses a base 16 number from a string.
 *
 * The string MUST be at least 6 bytes long as this function cannot check the
 * length of @data. Checking the size must be done in the caller.
 *
 * Return value: A parsed value, or 0 for error
 *
 * Since: 1.3.1
 **/
guint32
fu_firmware_strparse_uint24 (const gchar *data)
{
	gchar buffer[7];
	memcpy (buffer, data, 6);
	buffer[6] = '\0';
	return (guint32) g_ascii_strtoull (buffer, NULL, 16);
}

/**
 * fu_firmware_strparse_uint32:
 * @data: a string
 *
 * Parses a base 16 number from a string.
 *
 * The string MUST be at least 8 bytes long as this function cannot check the
 * length of @data. Checking the size must be done in the caller.
 *
 * Return value: A parsed value, or 0 for error
 *
 * Since: 1.3.1
 **/
guint32
fu_firmware_strparse_uint32 (const gchar *data)
{
	gchar buffer[9];
	memcpy (buffer, data, 8);
	buffer[8] = '\0';
	return (guint32) g_ascii_strtoull (buffer, NULL, 16);
}

/**
 * fu_firmware_strparse_uint4_safe:
 * @data: destination buffer
 * @datasz: size of @data, typcally the same as `strlen(data)`
 * @offset: offset in chars into @data to read
 * @value: (out) (nullable): parsed value
 * @error: A #GError or %NULL
 *
 * Parses a base 16 number from a string of 1 character in length.
 * The returned @value will range from from 0 to 0xf.
 *
 * Return value: %TRUE if parsed, %FALSE otherwise
 *
 * Since: 1.5.6
 **/
gboolean
fu_firmware_strparse_uint4_safe (const gchar *data,
				 gsize datasz,
				 gsize offset,
				 guint8 *value,
				 GError **error)
{
	gchar buffer[2] = { '\0' };
	if (!fu_memcpy_safe ((guint8 *) buffer, sizeof(buffer), 0x0,	/* dst */
			     (const guint8 *) data, datasz, offset,	/* src */
			     sizeof(buffer) - 1, error))
		return FALSE;
	if (value != NULL)
		*value = (guint8) g_ascii_strtoull (buffer, NULL, 16);
	return TRUE;
}

/**
 * fu_firmware_strparse_uint8_safe:
 * @data: destination buffer
 * @datasz: size of @data, typcally the same as `strlen(data)`
 * @offset: offset in chars into @data to read
 * @value: (out) (nullable): parsed value
 * @error: A #GError or %NULL
 *
 * Parses a base 16 number from a string of 2 characters in length.
 * The returned @value will range from from 0 to 0xff.
 *
 * Return value: %TRUE if parsed, %FALSE otherwise
 *
 * Since: 1.5.6
 **/
gboolean
fu_firmware_strparse_uint8_safe (const gchar *data,
				 gsize datasz,
				 gsize offset,
				 guint8 *value,
				 GError **error)
{
	gchar buffer[3] = { '\0' };
	if (!fu_memcpy_safe ((guint8 *) buffer, sizeof(buffer), 0x0,	/* dst */
			     (const guint8 *) data, datasz, offset,	/* src */
			     sizeof(buffer) - 1, error))
		return FALSE;
	if (value != NULL)
		*value = (guint8) g_ascii_strtoull (buffer, NULL, 16);
	return TRUE;
}

/**
 * fu_firmware_strparse_uint16_safe:
 * @data: destination buffer
 * @datasz: size of @data, typcally the same as `strlen(data)`
 * @offset: offset in chars into @data to read
 * @value: (out) (nullable): parsed value
 * @error: A #GError or %NULL
 *
 * Parses a base 16 number from a string of 4 characters in length.
 * The returned @value will range from from 0 to 0xffff.
 *
 * Return value: %TRUE if parsed, %FALSE otherwise
 *
 * Since: 1.5.6
 **/
gboolean
fu_firmware_strparse_uint16_safe (const gchar *data,
				  gsize datasz,
				  gsize offset,
				  guint16 *value,
				  GError **error)
{
	gchar buffer[5] = { '\0' };
	if (!fu_memcpy_safe ((guint8 *) buffer, sizeof(buffer), 0x0,	/* dst */
			     (const guint8 *) data, datasz, offset,	/* src */
			     sizeof(buffer) - 1, error))
		return FALSE;
	if (value != NULL)
		*value = (guint16) g_ascii_strtoull (buffer, NULL, 16);
	return TRUE;
}

/**
 * fu_firmware_strparse_uint24_safe:
 * @data: destination buffer
 * @datasz: size of @data, typcally the same as `strlen(data)`
 * @offset: offset in chars into @data to read
 * @value: (out) (nullable): parsed value
 * @error: A #GError or %NULL
 *
 * Parses a base 16 number from a string of 6 characters in length.
 * The returned @value will range from from 0 to 0xffffff.
 *
 * Return value: %TRUE if parsed, %FALSE otherwise
 *
 * Since: 1.5.6
 **/
gboolean
fu_firmware_strparse_uint24_safe (const gchar *data,
				  gsize datasz,
				  gsize offset,
				  guint32 *value,
				  GError **error)
{
	gchar buffer[7] = { '\0' };
	if (!fu_memcpy_safe ((guint8 *) buffer, sizeof(buffer), 0x0,	/* dst */
			     (const guint8 *) data, datasz, offset,	/* src */
			     sizeof(buffer) - 1, error))
		return FALSE;
	if (value != NULL)
		*value = (guint16) g_ascii_strtoull (buffer, NULL, 16);
	return TRUE;
}

/**
 * fu_firmware_strparse_uint32_safe:
 * @data: destination buffer
 * @datasz: size of @data, typcally the same as `strlen(data)`
 * @offset: offset in chars into @data to read
 * @value: (out) (nullable): parsed value
 * @error: A #GError or %NULL
 *
 * Parses a base 16 number from a string of 8 characters in length.
 * The returned @value will range from from 0 to 0xffffffff.
 *
 * Return value: %TRUE if parsed, %FALSE otherwise
 *
 * Since: 1.5.6
 **/
gboolean
fu_firmware_strparse_uint32_safe (const gchar *data,
				  gsize datasz,
				  gsize offset,
				  guint32 *value,
				  GError **error)
{
	gchar buffer[9] = { '\0' };
	if (!fu_memcpy_safe ((guint8 *) buffer, sizeof(buffer), 0x0,	/* dst */
			     (const guint8 *) data, datasz, offset,	/* src */
			     sizeof(buffer) - 1, error))
		return FALSE;
	if (value != NULL)
		*value = (guint32) g_ascii_strtoull (buffer, NULL, 16);
	return TRUE;
}
