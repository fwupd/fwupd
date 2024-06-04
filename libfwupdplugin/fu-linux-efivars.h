/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-efivars.h"

#define FU_TYPE_LINUX_EFIVARS (fu_linux_efivars_get_type())
G_DECLARE_FINAL_TYPE(FuLinuxEfivars, fu_linux_efivars, FU, LINUX_EFIVARS, FuEfivars)
