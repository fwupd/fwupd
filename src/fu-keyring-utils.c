/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuKeyring"

#include "config.h"

#include <string.h>

#include "fu-keyring-utils.h"

/**
 * fu_keyring_get_release_flags:
 * @release: the reelase node
 * @flags: (out): flags for the release, e.g. %FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD
 * @error: (nullable): optional return location for an error
 *
 * Uses the correct keyring to get the trust flags for a given release.
 *
 * Returns: %TRUE if @flags has been set
 **/
gboolean
fu_keyring_get_release_flags(XbNode *release, FwupdReleaseFlags *flags, GError **error)
{
	GBytes *blob;

	blob = g_object_get_data(G_OBJECT(release), "fwupd::ReleaseFlags");
	if (blob == NULL)
		return TRUE;
	if (g_bytes_get_size(blob) != sizeof(FwupdReleaseFlags)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid fwupd::ReleaseFlags set by loader");
		return FALSE;
	}
	memcpy(flags, g_bytes_get_data(blob, NULL), sizeof(FwupdReleaseFlags));
	return TRUE;
}
