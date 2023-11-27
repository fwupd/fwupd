/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-backend.h"

gboolean
fu_backend_load(FuBackend *self,
		JsonObject *json_object,
		const gchar *tag,
		FuBackendLoadFlags flags,
		GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_backend_save(FuBackend *self,
		JsonBuilder *json_builder,
		const gchar *tag,
		FuBackendSaveFlags flags,
		GError **error) G_GNUC_NON_NULL(1, 2);
