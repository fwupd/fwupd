/*
 * Copyright (C) 2015-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuFirmware"

#include <config.h>

#include <string.h>

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
