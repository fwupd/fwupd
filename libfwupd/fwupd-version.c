/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fwupd-version.h"

/**
 * fwupd_version_string:
 *
 * Gets the libfwupd installed runtime version.
 *
 * This may be different to the *build-time* version if the daemon and library
 * objects somehow get out of sync.
 *
 * Returns: version string
 *
 * Since: 1.6.1
 **/
const gchar *
fwupd_version_string (void)
{
	return G_STRINGIFY(FWUPD_MAJOR_VERSION) "."
		G_STRINGIFY(FWUPD_MINOR_VERSION) "."
		G_STRINGIFY(FWUPD_MICRO_VERSION);
}
