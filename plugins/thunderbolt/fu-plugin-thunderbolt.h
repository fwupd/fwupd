/* -*- mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Christian J. Kellner <christian@kellner.me>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_PLUGIN_THUNDERBOLT_H__
#define __FU_PLUGIN_THUNDERBOLT_H__

#include "fu-plugin.h"

#define FU_PLUGIN_THUNDERBOLT_UPDATE_TIMEOUT_MS 60 * 1000

void        fu_plugin_thunderbolt_set_timeout (FuPlugin *plugin,
					       guint     timeout_ms);

#endif /* __FU_PLUGIN_THUNDERBOLT_H__ */
