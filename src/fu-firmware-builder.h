/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

GBytes *
fu_firmware_builder_process(GBytes *bytes,
			    const gchar *script_fn,
			    const gchar *output_fn,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;
