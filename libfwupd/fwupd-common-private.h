/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#include "fwupd-common.h"

G_BEGIN_DECLS

gchar		*fwupd_checksum_format_for_display	(const gchar	*checksum);

G_END_DECLS
