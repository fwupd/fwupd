/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-intel-me-common.h"

gboolean
fu_intel_me_mkhi_result_to_error(FuMkhiResult result, GError **error)
{
	if (result == MKHI_STATUS_SUCCESS)
		return TRUE;

	switch (result) {
	case MKHI_STATUS_NOT_SUPPORTED:
	case MKHI_STATUS_NOT_AVAILABLE:
	case MKHI_STATUS_NOT_SET:
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "not supported [0x%x]",
			    result);
		break;
	default:
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "generic failure [0x%x]", result);
		break;
	}
	return FALSE;
}

gboolean
fu_intel_me_mkhi_verify_header(const FuMkhiHeader *hdr_req,
			       const FuMkhiHeader *hdr_res,
			       GError **error)
{
	if (hdr_req->group_id != hdr_res->group_id) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid response group ID, requested 0x%x and got 0x%x",
			    hdr_req->group_id,
			    hdr_res->group_id);
		return FALSE;
	}
	if (hdr_req->command != hdr_res->command) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid response command, requested 0x%x and got 0x%x",
			    (guint)hdr_req->command,
			    (guint)hdr_res->command);
		return FALSE;
	}
	if (!hdr_res->is_resp) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "invalid response group ID, not a response!");
		return FALSE;
	}
	return fu_intel_me_mkhi_result_to_error(hdr_res->result, error);
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
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_INITIALIZED,
				    "buffer was all 0x00");
		return NULL;
	}
	if (!seen_nonff_data) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_INITIALIZED,
				    "buffer was all 0xFF");
		return NULL;
	}

	/* success */
	return g_steal_pointer(&checksum);
}
