/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FWUPD_COMMON_PRIVATE_H
#define __FWUPD_COMMON_PRIVATE_H

#include <glib.h>

#include "fwupd-common.h"

gchar		*fwupd_checksum_format_for_display	(const gchar	*checksum);

#endif /* __FWUPD_COMMON_PRIVATE_H */
