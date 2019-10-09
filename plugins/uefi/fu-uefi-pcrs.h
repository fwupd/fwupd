/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#define FU_TYPE_UEFI_PCRS (fu_uefi_pcrs_get_type ())
G_DECLARE_FINAL_TYPE (FuUefiPcrs, fu_uefi_pcrs, FU, UEFI_PCRS, GObject)

FuUefiPcrs	*fu_uefi_pcrs_new		(void);
gboolean	 fu_uefi_pcrs_setup		(FuUefiPcrs	*self,
						 GError		**error);
GPtrArray	*fu_uefi_pcrs_get_checksums	(FuUefiPcrs	*self,
						 guint		 idx);
