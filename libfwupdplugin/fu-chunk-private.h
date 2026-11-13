/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <xmlb.h>

#include "fu-chunk.h"
#include "fu-firmware.h"

G_BEGIN_DECLS

void
fu_chunk_set_data_sz(FuChunk *self, gsize data_sz) G_GNUC_NON_NULL(1);
void
fu_chunk_export(FuChunk *self, FuFirmwareExportFlags flags, FwupdJsonObject *json_object)
    G_GNUC_NON_NULL(1, 3);
gboolean
fu_chunk_build(FuChunk *self, FwupdJsonObject *json_object, GError **error) G_GNUC_NON_NULL(1, 2);

G_END_DECLS
