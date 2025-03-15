/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

const gchar *
fu_cpu_amd_stream_name_to_entry_sign_fixed_agesa_version(const gchar *stream_name);
guint32
fu_cpu_amd_model_id_to_entry_sign_fixed_ucode_version(guint32 model_id);
