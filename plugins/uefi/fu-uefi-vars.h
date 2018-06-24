/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_UEFI_VARS_H
#define __FU_UEFI_VARS_H

#include <glib.h>

G_BEGIN_DECLS

#define FU_UEFI_EFI_GLOBAL_GUID			"8be4df61-93ca-11d2-aa0d-00e098032b8c"

gboolean	 fu_uefi_vars_supported		(GError		**error);
gboolean	 fu_uefi_vars_exists		(const gchar	*guid,
						 const gchar	*name);
gboolean	 fu_uefi_vars_get_data		(const gchar	*guid,
						 const gchar	*name,
						 guint8		**data,
						 gsize		*sz,
						 GError		**error);
gboolean	 fu_uefi_vars_set_data		(const gchar	*guid,
						 const gchar	*name,
						 const guint8	*data,
						 gsize		 sz,
						 GError		**error);

G_END_DECLS

#endif /* __FU_UEFI_VARS_H */
