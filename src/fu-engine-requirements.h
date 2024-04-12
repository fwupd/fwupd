/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-engine.h"
#include "fu-release.h"

gboolean
fu_engine_requirements_check(FuEngine *engine,
			     FuRelease *release,
			     FwupdInstallFlags flags,
			     GError **error) G_GNUC_NON_NULL(1, 2);
