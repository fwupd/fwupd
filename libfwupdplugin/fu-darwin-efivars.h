/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-efivars.h"

#define FU_TYPE_DARWIN_EFIVARS (fu_darwin_efivars_get_type())
G_DECLARE_FINAL_TYPE(FuDarwinEfivars, fu_darwin_efivars, FU, DARWIN_EFIVARS, FuEfivars)
