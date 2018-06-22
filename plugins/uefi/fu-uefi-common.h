/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_UEFI_COMMON_H
#define __FU_UEFI_COMMON_H

#include <glib.h>

G_BEGIN_DECLS

guint64		 fu_uefi_read_file_as_uint64	(const gchar	*path,
						 const gchar	*attr_name);

G_END_DECLS

#endif /* __FU_UEFI_COMMON_H */
