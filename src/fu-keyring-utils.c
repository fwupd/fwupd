/*
 * Copyright (C) 2017-2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuKeyring"

#include <config.h>
#include <string.h>

#include "fwupd-error.h"

#include "fu-keyring-utils.h"

/**
 * fu_keyring_get_release_flags:
 * @release: A #XbNode, e.g. %FWUPD_KEYRING_KIND_GPG
 * @flags: A #FwupdReleaseFlags, e.g. %FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD
 * @error: A #GError, or %NULL
 *
 * Uses the correct keyring to get the trust flags for a given release.
 *
 * Returns: %TRUE if @flags has been set
 **/
gboolean
fu_keyring_get_release_flags (XbNode *release,
			      FwupdReleaseFlags *flags,
			      GError **error)
{
	GBytes *blob;

	blob = g_object_get_data (G_OBJECT (release), "fwupd::ReleaseFlags");
	if (blob == NULL)
		return TRUE;
	if (g_bytes_get_size (blob) != sizeof(FwupdReleaseFlags)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "invalid fwupd::ReleaseFlags set by loader");
		return FALSE;
	}
	memcpy (flags, g_bytes_get_data (blob, NULL), sizeof(FwupdReleaseFlags));
	return TRUE;
}
