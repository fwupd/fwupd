/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-fuzzer.h"

G_DEFINE_INTERFACE(FuFuzzer, fu_fuzzer, G_TYPE_OBJECT)

static void
fu_fuzzer_default_init(FuFuzzerInterface *iface)
{
}

/**
 * fu_fuzzer_test_input:
 * @self: a #FuFuzzer
 * @blob: a #GBytes
 * @error: (nullable): optional return location for an error
 *
 * Calls the implementation with the contents of a fuzzing buffer.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.1.1
 */
gboolean
fu_fuzzer_test_input(FuFuzzer *self, GBytes *blob, GError **error)
{
	FuFuzzerInterface *iface = FU_FUZZER_GET_IFACE(self);
	if (iface->test_input == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "FuFuzzer->test_input() not implemented");
		return FALSE;
	}
	return iface->test_input(self, blob, error);
}

/**
 * fu_fuzzer_build_example:
 * @self: a #FuFuzzer
 * @blob: a #GBytes to use as an input buffer, or %NULL
 * @error: (nullable): optional return location for an error
 *
 * Builds a sample corpus used for fuzzing.
 *
 * Returns: (transfer full): a #GBytes on success
 *
 * Since: 2.1.1
 */
GBytes *
fu_fuzzer_build_example(FuFuzzer *self, GBytes *blob, GError **error)
{
	FuFuzzerInterface *iface = FU_FUZZER_GET_IFACE(self);
	if (iface->build_example == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "FuFuzzer->build_example() not implemented");
		return NULL;
	}
	return iface->build_example(self, blob, error);
}
