/*
 * Copyright 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <gio/gio.h>

#include "fwupd-common.h"
#include "fwupd-error.h"

/**
 * fwupd_error_quark:
 *
 * An error quark.
 *
 * Returns: an error quark
 *
 * Since: 0.1.1
 **/
GQuark
fwupd_error_quark(void)
{
	static GQuark quark = 0;
	if (!quark) {
		quark = g_quark_from_static_string("FwupdError");
		for (gint i = 0; i < FWUPD_ERROR_LAST; i++) {
			g_dbus_error_register_error(quark, i, fwupd_error_to_string(i));
		}
	}
	return quark;
}

/**
 * fwupd_error_convert:
 * @perror: (nullable): A #GError, perhaps with domain #GIOError
 *
 * Convert the error to a #FwupdError, if required.
 *
 * Since: 2.0.0
 **/
void
fwupd_error_convert(GError **perror)
{
	GError *error = (perror != NULL) ? *perror : NULL;
	struct {
		GQuark domain;
		gint code;
		FwupdError fwupd_code;
	} map[] = {
	    {G_FILE_ERROR, G_FILE_ERROR_ACCES, FWUPD_ERROR_PERMISSION_DENIED},
	    {G_FILE_ERROR, G_FILE_ERROR_AGAIN, FWUPD_ERROR_BUSY},
	    {G_FILE_ERROR, G_FILE_ERROR_BADF, FWUPD_ERROR_INTERNAL},
	    {G_FILE_ERROR, G_FILE_ERROR_EXIST, FWUPD_ERROR_PERMISSION_DENIED},
	    {G_FILE_ERROR, G_FILE_ERROR_FAILED, FWUPD_ERROR_INTERNAL},
	    {G_FILE_ERROR, G_FILE_ERROR_FAULT, FWUPD_ERROR_INTERNAL},
	    {G_FILE_ERROR, G_FILE_ERROR_INTR, FWUPD_ERROR_BUSY},
	    {G_FILE_ERROR, G_FILE_ERROR_INVAL, FWUPD_ERROR_INVALID_DATA},
	    {G_FILE_ERROR, G_FILE_ERROR_IO, FWUPD_ERROR_INTERNAL},
	    {G_FILE_ERROR, G_FILE_ERROR_ISDIR, FWUPD_ERROR_INVALID_FILE},
	    {G_FILE_ERROR, G_FILE_ERROR_LOOP, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_FILE_ERROR, G_FILE_ERROR_MFILE, FWUPD_ERROR_INTERNAL},
	    {G_FILE_ERROR, G_FILE_ERROR_NAMETOOLONG, FWUPD_ERROR_INVALID_DATA},
	    {G_FILE_ERROR, G_FILE_ERROR_NFILE, FWUPD_ERROR_BROKEN_SYSTEM},
	    {G_FILE_ERROR, G_FILE_ERROR_NODEV, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_FILE_ERROR, G_FILE_ERROR_NOENT, FWUPD_ERROR_INVALID_FILE},
	    {G_FILE_ERROR, G_FILE_ERROR_NOSPC, FWUPD_ERROR_BROKEN_SYSTEM},
	    {G_FILE_ERROR, G_FILE_ERROR_NOSYS, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_FILE_ERROR, G_FILE_ERROR_NOTDIR, FWUPD_ERROR_INVALID_FILE},
	    {G_FILE_ERROR, G_FILE_ERROR_NXIO, FWUPD_ERROR_NOT_FOUND},
	    {G_FILE_ERROR, G_FILE_ERROR_PERM, FWUPD_ERROR_PERMISSION_DENIED},
	    {G_FILE_ERROR, G_FILE_ERROR_PIPE, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_FILE_ERROR, G_FILE_ERROR_ROFS, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_FILE_ERROR, G_FILE_ERROR_TXTBSY, FWUPD_ERROR_BUSY},
	    {G_IO_ERROR, G_IO_ERROR_BUSY, FWUPD_ERROR_TIMED_OUT},
	    {G_IO_ERROR, G_IO_ERROR_CANCELLED, FWUPD_ERROR_INTERNAL},
	    {G_IO_ERROR, G_IO_ERROR_FAILED, FWUPD_ERROR_INTERNAL},
	    {G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, FWUPD_ERROR_INVALID_DATA},
	    {G_IO_ERROR, G_IO_ERROR_INVALID_DATA, FWUPD_ERROR_INVALID_DATA},
#if GLIB_CHECK_VERSION(2, 74, 0)
	    {G_IO_ERROR, G_IO_ERROR_NO_SUCH_DEVICE, FWUPD_ERROR_NOT_FOUND},
#endif
	    {G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, FWUPD_ERROR_NOT_FOUND},
	    {G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_IO_ERROR, G_IO_ERROR_NOT_FOUND, FWUPD_ERROR_NOT_FOUND},
	    {G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED, FWUPD_ERROR_INTERNAL},
	    {G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE, FWUPD_ERROR_INVALID_DATA},
	    {G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT, FWUPD_ERROR_READ},
	    {G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, FWUPD_ERROR_PERMISSION_DENIED},
	    {G_IO_ERROR, G_IO_ERROR_TIMED_OUT, FWUPD_ERROR_TIMED_OUT},
	    {G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK, FWUPD_ERROR_TIMED_OUT},
	    {G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_UNKNOWN_ENCODING, FWUPD_ERROR_INVALID_DATA},
	    {G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE, FWUPD_ERROR_INVALID_DATA},
	    {G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_NOT_FOUND, FWUPD_ERROR_INVALID_DATA},
	    {G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND, FWUPD_ERROR_INVALID_DATA},
	    {G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND, FWUPD_ERROR_INVALID_DATA},
	    {G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE, FWUPD_ERROR_INVALID_DATA},
	    {G_MARKUP_ERROR, G_MARKUP_ERROR_BAD_UTF8, FWUPD_ERROR_INVALID_DATA},
	    {G_MARKUP_ERROR, G_MARKUP_ERROR_EMPTY, FWUPD_ERROR_INVALID_DATA},
	    {G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT, FWUPD_ERROR_INVALID_DATA},
	    {G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE, FWUPD_ERROR_INVALID_DATA},
	    {G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE, FWUPD_ERROR_INVALID_DATA},
	    {G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE, FWUPD_ERROR_NOT_SUPPORTED},
	    {G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT, FWUPD_ERROR_NOT_SUPPORTED},
	};

	/* sanity check */
	if (error == NULL)
		return;
	if (error->domain == FWUPD_ERROR)
		return;

	/* correct some company names */
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		if (g_error_matches(error, map[i].domain, map[i].code)) {
			error->domain = FWUPD_ERROR;
			error->code = map[i].fwupd_code;
			return;
		}
	}

	/* fallback */
#ifndef SUPPORTED_BUILD
	g_critical("GError %s:%i sending over D-Bus was not converted to FwupdError",
		   g_quark_to_string(error->domain),
		   error->code);
#endif
	error->domain = FWUPD_ERROR;
	error->code = FWUPD_ERROR_INTERNAL;
}

/**
 * fwupd_strerror:
 *
 * Returns an untranslated string corresponding to the given error code, e.g. “no such process”.
 *
 * Returns: string describing the error code
 *
 * Since: 2.0.11
 **/
const gchar *
fwupd_strerror(gint errnum) /* nocheck:name */
{
#ifdef HAVE_STRERRORDESC_NP
	return strerrordesc_np(errnum);
#else
	return g_strerror(errnum); /* nocheck:blocked */
#endif
}
