/*
 * Copyright (C) 2017 Intel Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

typedef enum {
	VALIDATION_PASSED,
	VALIDATION_FAILED,
	UNKNOWN_DEVICE,
} FuPluginValidation;

/* byte offsets in firmware image */
#define FU_TBT_OFFSET_NATIVE		0x7B
#define FU_TBT_CHUNK_SZ			0x40

FuPluginValidation	fu_thunderbolt_image_validate		(GBytes  *controller_fw,
								 GBytes  *blob_fw,
								 GError **error);

gboolean	fu_thunderbolt_image_controller_is_native	(GBytes    *controller_fw,
								 gboolean  *is_native,
								 GError   **error);
