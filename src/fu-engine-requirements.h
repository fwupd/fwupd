/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-engine.h"
#include "fu-release.h"

gboolean
fu_engine_requirements_check(FuEngine *engine,
			     FuRelease *release,
			     FwupdInstallFlags flags,
			     GError **error);
