/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-efivars.h"

#define FU_TYPE_WINDOWS_EFIVARS (fu_windows_efivars_get_type())
G_DECLARE_FINAL_TYPE(FuWindowsEfivars, fu_windows_efivars, FU, WINDOWS_EFIVARS, FuEfivars)
