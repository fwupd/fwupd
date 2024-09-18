/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-efivars.h"

#define FU_TYPE_DUMMY_EFIVARS (fu_dummy_efivars_get_type())
G_DECLARE_FINAL_TYPE(FuDummyEfivars, fu_dummy_efivars, FU, DUMMY_EFIVARS, FuEfivars)

FuEfivars *
fu_dummy_efivars_new(void);
