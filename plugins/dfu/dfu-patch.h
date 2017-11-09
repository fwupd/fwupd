/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __DFU_PATCH_H
#define __DFU_PATCH_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define DFU_TYPE_PATCH (dfu_patch_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuPatch, dfu_patch, DFU, PATCH, GObject)

struct _DfuPatchClass
{
	GObjectClass		 parent_class;
};

/**
 * DfuPatchApplyFlags:
 * @DFU_PATCH_APPLY_FLAG_NONE:			No flags set
 * @DFU_PATCH_APPLY_FLAG_IGNORE_CHECKSUM:	Do not check the checksum
 *
 * The optional flags used for applying a patch.
 **/
typedef enum {
	DFU_PATCH_APPLY_FLAG_NONE		= 0,
	DFU_PATCH_APPLY_FLAG_IGNORE_CHECKSUM	= (1 << 0),
	/*< private >*/
	DFU_PATCH_APPLY_FLAG_LAST
} DfuPatchApplyFlags;

DfuPatch	*dfu_patch_new			(void);

gchar		*dfu_patch_to_string		(DfuPatch	*self);
GBytes		*dfu_patch_export		(DfuPatch	*self,
						 GError		**error);
gboolean	 dfu_patch_import		(DfuPatch	*self,
						 GBytes		*blob,
						 GError		**error);
gboolean	 dfu_patch_create		(DfuPatch	*self,
						 GBytes		*blob1,
						 GBytes		*blob2,
						 GError		**error);
GBytes		*dfu_patch_apply		(DfuPatch	*self,
						 GBytes		*blob,
						 DfuPatchApplyFlags flags,
						 GError		**error);
GBytes		*dfu_patch_get_checksum_old	(DfuPatch	*self);
GBytes		*dfu_patch_get_checksum_new	(DfuPatch	*self);

G_END_DECLS

#endif /* __DFU_PATCH_H */
