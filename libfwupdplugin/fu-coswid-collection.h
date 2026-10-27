/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-linear-firmware.h"

G_BEGIN_DECLS

#define FU_TYPE_COSWID_COLLECTION (fu_coswid_collection_get_type())
G_DECLARE_FINAL_TYPE(FuCoswidCollection,
		     fu_coswid_collection,
		     FU,
		     COSWID_COLLECTION,
		     FuLinearFirmware)

FuFirmware *
fu_coswid_collection_new(void);

G_END_DECLS
