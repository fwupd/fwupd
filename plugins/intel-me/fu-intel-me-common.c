/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupd.h>

#include "fu-intel-me-common.h"
#include "fu-intel-me-mkhi-struct.h"

gboolean
fu_intel_me_mkhi_result_to_error(FuMkhiStatus result, GError **error)
{
	if (result == FU_MKHI_STATUS_SUCCESS)
		return TRUE;

	switch (result) {
	case FU_MKHI_STATUS_NOT_SUPPORTED:
	case FU_MKHI_STATUS_NOT_AVAILABLE:
	case FU_MKHI_STATUS_NOT_SET:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "not supported [0x%x]",
			    result);
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "generic failure [0x%x]",
			    result);
		break;
	}
	return FALSE;
}

GString *
fu_intel_me_convert_checksum(GByteArray *buf, GError **error)
{
	gboolean seen_non00_data = FALSE;
	gboolean seen_nonff_data = FALSE;
	g_autoptr(GString) checksum = g_string_new(NULL);

	/* create checksum, but only if non-zero and set */
	for (gsize i = 0; i < buf->len; i++) {
		if (!seen_non00_data && buf->data[i] != 0x00)
			seen_non00_data = TRUE;
		if (!seen_nonff_data && buf->data[i] != 0xFF)
			seen_nonff_data = TRUE;
		g_string_append_printf(checksum, "%02x", buf->data[i]);
	}
	if (!seen_non00_data) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "buffer was all 0x00");
		return NULL;
	}
	if (!seen_nonff_data) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "buffer was all 0xFF");
		return NULL;
	}

	/* success */
	return g_steal_pointer(&checksum);
}
