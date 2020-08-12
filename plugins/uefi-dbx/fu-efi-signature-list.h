/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-efi-signature.h"

#define FU_TYPE_EFI_SIGNATURE_LIST (fu_efi_signature_list_get_type ())
G_DECLARE_FINAL_TYPE (FuEfiSignatureList, fu_efi_signature_list, FU, EFI_SIGNATURE_LIST, GObject)


FuEfiSignatureList*fu_efi_signature_list_new		(FuEfiSignatureKind kind);
FuEfiSignatureKind fu_efi_signature_list_get_kind	(FuEfiSignatureList	*self);
void		 fu_efi_signature_list_add		(FuEfiSignatureList	*self,
							 FuEfiSignature		*signature);
GPtrArray	*fu_efi_signature_list_get_all		(FuEfiSignatureList	*self);
gboolean	 fu_efi_signature_list_has_checksum	(FuEfiSignatureList	*self,
							 const gchar		*checksum);
gboolean	 fu_efi_signature_list_are_inclusive	(FuEfiSignatureList	*self,
							 FuEfiSignatureList	*other);
