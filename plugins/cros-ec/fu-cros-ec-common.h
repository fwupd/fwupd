/*
 * Copyright 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_CROS_EC_MAX_BLOCK_XFER_RETRIES  10
#define FU_CROS_EC_FLUSH_TIMEOUT_MS	   10
#define FU_CROS_EC_BULK_SEND_TIMEOUT	   2000 /* ms */
#define FU_CROS_EC_BULK_RECV_TIMEOUT	   5000 /* ms */
#define FU_CROS_EC_USB_DEVICE_REMOVE_DELAY 20000

#define FU_CROS_EC_REQUEST_UPDATE_DONE	    0xB007AB1E
#define FU_CROS_EC_REQUEST_UPDATE_EXTRA_CMD 0xB007AB1F

typedef struct {
	gchar *boardname;
	gchar *triplet;
	gchar *sha1;
	gboolean dirty;
} FuCrosEcVersion;

FuCrosEcVersion *
fu_cros_ec_version_parse(const gchar *version_raw, GError **error);
void
fu_cros_ec_version_free(FuCrosEcVersion *version);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCrosEcVersion, fu_cros_ec_version_free)
