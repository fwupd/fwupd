/*
 * Copyright (C) 2021 3mdeb Embedded Systems Consulting
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#define FU_TYPE_UEFI_ESRT (fu_uefi_esrt_get_type ())
G_DECLARE_FINAL_TYPE (FuUefiEsrt, fu_uefi_esrt, FU, UEFI_ESRT, GObject)

#define FU_TYPE_UEFI_ESRT_ENTRY (fu_uefi_esrt_entry_get_type ())
G_DECLARE_FINAL_TYPE (FuUefiEsrtEntry, fu_uefi_esrt_entry, FU, UEFI_ESRT_ENTRY, GObject)

FuUefiEsrt	*fu_uefi_esrt_new		(void);
gboolean	 fu_uefi_esrt_setup		(FuUefiEsrt	*self,
						 GError		**error);
guint		 fu_uefi_esrt_get_entry_count  	(FuUefiEsrt *self);
FuUefiEsrtEntry	*fu_uefi_esrt_get_entry		(FuUefiEsrt	*self,
						 guint		idx);

const gchar	*fu_uefi_esrt_entry_get_id		(FuUefiEsrtEntry *self);
gchar		*fu_uefi_esrt_entry_get_class		(FuUefiEsrtEntry *self);
guint32		 fu_uefi_esrt_entry_get_kind		(FuUefiEsrtEntry *self);
guint32		 fu_uefi_esrt_entry_get_capsule_flags	(FuUefiEsrtEntry *self);
guint32		 fu_uefi_esrt_entry_get_version		(FuUefiEsrtEntry *self);
guint32		 fu_uefi_esrt_entry_get_version_lowest	(FuUefiEsrtEntry *self);
guint32		 fu_uefi_esrt_entry_get_status		(FuUefiEsrtEntry *self);
guint32		 fu_uefi_esrt_entry_get_version_error	(FuUefiEsrtEntry *self);
