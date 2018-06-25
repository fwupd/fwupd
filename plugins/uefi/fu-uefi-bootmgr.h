/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_UEFI_BOOTMGR_H
#define __FU_UEFI_BOOTMGR_H

#include <glib.h>
#include <efivar.h>

G_BEGIN_DECLS

gboolean	 fu_uefi_bootmgr_bootnext	(const gchar	*esp_path,
						 GError		**error);

G_END_DECLS

#endif /* __FU_UEFI_BOOTMGR_H */
