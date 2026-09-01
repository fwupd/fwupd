/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#ifdef HAVE_GNUTLS
#include <gnutls/crypto.h>
#endif
#ifdef HAVE_LIBCRYPTO
#include <openssl/crypto.h>
#endif

#include "fwupd-error.h"

#include "fu-bytes.h"
#include "fu-path.h"
#include "fu-secure-bytes.h"

struct FuSecureBytes {
	guint8 *buf;
	gsize bufsz;
	GDestroyNotify destroy_fn;
};

/**
 * fu_secure_bytes_new: (skip):
 * @buf: (transfer full) (nullable): initial data that is moved into @self, typically stolen
 * @bufsz: size of @buf
 * @destroy_fn: (nullable): Function to call on @buf when the #FuSecureBytes is destroyed
 *
 * Creates a secure byte array that is cleared when deallocated.
 *
 * After this call, data belongs to the #FuSecureBytes and may no longer be modified by the caller.
 *
 * Returns: (transfer full): a #FuSecureBytes
 *
 * Since: 2.1.8
 **/
FuSecureBytes *
fu_secure_bytes_new(guint8 *buf, gsize bufsz, GDestroyNotify destroy_fn)
{
	FuSecureBytes *self = g_new0(FuSecureBytes, 1);
	if (buf != NULL) {
		self->buf = buf;
		self->bufsz = bufsz;
	}
	self->destroy_fn = destroy_fn;
	return self;
}

static void
fu_secure_bytes_memzero(guint8 *buf, gsize bufsz)
{
	if (buf == NULL || bufsz == 0)
		return;
#ifdef HAVE_GNUTLS
	gnutls_memset(buf, 0x0, bufsz);
#elif defined HAVE_LIBCRYPTO
	OPENSSL_cleanse(buf, bufsz);
#else
	memset(buf, 0x0, bufsz);
#endif
}

/**
 * fu_secure_bytes_free:
 * @self: a #FuSecureBytes
 *
 * Clears the contents to zero before the backing buffer is unreffed.
 *
 * Use this, typically via `g_autoptr(FuSecureBytes)`, for buffers holding
 * sensitive data such as keys, tokens or passwords so that the plaintext is not
 * left behind in freed heap memory.
 *
 * Since: 2.1.8
 **/
void
fu_secure_bytes_free(FuSecureBytes *self)
{
	g_return_if_fail(self != NULL);
	if (self->buf != NULL)
		fu_secure_bytes_memzero(self->buf, self->bufsz);
	if (self->destroy_fn != NULL)
		self->destroy_fn(self->buf);
	g_free(self);
}

/**
 * fu_secure_bytes_get_size:
 * @self: a #FuSecureBytes
 *
 * Returns the size of the secure buffer.
 *
 * Returns: size in bytes
 *
 * Since: 2.2.1
 **/
gsize
fu_secure_bytes_get_size(FuSecureBytes *self)
{
	g_return_val_if_fail(self != NULL, G_MAXSIZE);
	return self->bufsz;
}

/**
 * fu_secure_bytes_get_data:
 * @self: a #FuSecureBytes
 *
 * Returns the data of the secure buffer.
 *
 * Returns: pointer
 *
 * Since: 2.2.1
 **/
const guint8 *
fu_secure_bytes_get_data(FuSecureBytes *self)
{
	g_return_val_if_fail(self != NULL, NULL);
	return self->buf;
}

/**
 * fu_secure_bytes_get_contents: (skip):
 * @filename: a filename
 * @error: (nullable): optional return location for an error
 *
 * Reads a blob of data from a file into a #FuSecureBytes, clearing any temporary
 * copy of the plaintext so it is not left behind in freed heap memory.
 *
 * Use this, rather than fu_bytes_get_contents(), for sensitive data such as
 * keys, tokens or passwords, holding the result via `g_autoptr(FuSecureBytes)`.
 *
 * Returns: (transfer full): a #FuSecureBytes, or %NULL for failure
 *
 * Since: 2.1.8
 **/
FuSecureBytes *
fu_secure_bytes_get_contents(const gchar *filename, GError **error)
{
	gchar *data = NULL;
	gsize len = 0;

	g_return_val_if_fail(filename != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents(filename, &data, &len, error)) {
		fwupd_error_convert(error);
		return NULL;
	}
	return fu_secure_bytes_new((guint8 *)data, len, g_free);
}

/**
 * fu_secure_bytes_set_contents:
 * @self: a #FuSecureBytes
 * @filename: a filename
 * @mode: file permission, e.g. `0600`
 * @error: (nullable): optional return location for an error
 *
 * Writes a #FuSecureBytes to a filename with specific permissions, creating the
 * parent directories as required.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.2.1
 **/
gboolean
fu_secure_bytes_set_contents(FuSecureBytes *self, const gchar *filename, gint mode, GError **error)
{
	g_return_val_if_fail(self != NULL, FALSE);
	g_return_val_if_fail(self->buf != NULL, FALSE);
	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_path_mkdir_parent(filename, error))
		return FALSE;
#if GLIB_CHECK_VERSION(2, 66, 0)
	if (!g_file_set_contents_full(filename,
				      (gchar *)self->buf,
				      self->bufsz,
				      G_FILE_SET_CONTENTS_CONSISTENT,
				      mode,
				      error)) {
		fwupd_error_convert(error);
		return FALSE;
	}
#else
	if (!g_file_set_contents(filename, (gchar *)self->buf, self->bufsz, error)) {
		fwupd_error_convert(error);
		return FALSE;
	}
	if (g_chmod(filename, mode) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to set file mode: %s",
			    fwupd_strerror(errno));
		return FALSE;
	}
#endif
	/* success */
	return TRUE;
}
