/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#define FU_TYPE_UEFI_BGRT (fu_uefi_bgrt_get_type ())
G_DECLARE_FINAL_TYPE (FuUefiBgrt, fu_uefi_bgrt, FU, UEFI_BGRT, GObject)

FuUefiBgrt	*fu_uefi_bgrt_new		(void);
gboolean	 fu_uefi_bgrt_setup		(FuUefiBgrt	*self,
						 GError		**error);
gboolean	 fu_uefi_bgrt_get_supported	(FuUefiBgrt	*self);
guint32		 fu_uefi_bgrt_get_xoffset	(FuUefiBgrt	*self);
guint32		 fu_uefi_bgrt_get_yoffset	(FuUefiBgrt	*self);
guint32		 fu_uefi_bgrt_get_width		(FuUefiBgrt	*self);
guint32		 fu_uefi_bgrt_get_height	(FuUefiBgrt	*self);
