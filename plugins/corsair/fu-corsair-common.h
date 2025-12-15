/*
 * Copyright 2022 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_CORSAIR_MAX_CMD_SIZE 1024

gchar *
fu_corsair_version_from_uint32(guint32 val);
