/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#define FU_TYPE_FUZZER (fu_fuzzer_get_type())
G_DECLARE_INTERFACE(FuFuzzer, fu_fuzzer, FU, FUZZER, GObject)

struct _FuFuzzerInterface {
	GTypeInterface g_iface;
	gboolean (*test_input)(FuFuzzer *self, GBytes *blob, GError **error);
	GBytes *(*build_example)(FuFuzzer *self, GBytes *blob, GError **error);
};

gboolean
fu_fuzzer_test_input(FuFuzzer *self, GBytes *blob, GError **error);
GBytes *
fu_fuzzer_build_example(FuFuzzer *self, GBytes *blob, GError **error);
