/*
 * Copyright (C) 2017 Kate Hsuan <hpa@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fwupd-security-attr-private.h"

#include "fu-engine.h"

gboolean
fu_engine_security_harden(FuEngine *self,
			  const gchar *appstream_id,
			  gboolean do_fix,
			  GError **error);
