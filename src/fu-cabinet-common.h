/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <xmlb.h>

XbSilo *
fu_cabinet_build_silo(GBytes *blob, guint64 size_max, GError **error) G_GNUC_WARN_UNUSED_RESULT;
