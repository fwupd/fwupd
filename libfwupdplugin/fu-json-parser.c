/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-json-parser.h"

/**
 * fu_json_parser_load_from_stream: (skip):
 * @self: a #FwupdJsonParser
 * @stream: a #FuInputStream
 * @flags: a #FwupdJsonLoadFlags
 * @error: (nullable): optional return location for an error
 *
 * Loads JSON from a stream.
 *
 * Returns: (transfer full): a #FwupdJsonNode, or %NULL for error
 *
 * Since: 2.1.8
 **/
FwupdJsonNode *
fu_json_parser_load_from_stream(FwupdJsonParser *self,
				FuInputStream *stream,
				FwupdJsonLoadFlags flags,
				GError **error)
{
	FuRsStreamImpl *impl;

	g_return_val_if_fail(FWUPD_IS_JSON_PARSER(self), NULL);
	g_return_val_if_fail(FU_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	impl = fu_input_stream_get_stream_impl(stream);
	if (impl == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "stream does not have a Rust implementation");
		return NULL;
	}

	/* ownership of impl is transferred */
	return fwupd_json_parser_load_from_stream_impl(self, impl, flags, error);
}
