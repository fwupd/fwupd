/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#include "fu-firmware.h"

#define FU_TYPE_EFI_SIGNATURE (fu_efi_signature_get_type ())
G_DECLARE_FINAL_TYPE (FuEfiSignature, fu_efi_signature, FU, EFI_SIGNATURE, FuFirmware)

typedef enum {
	FU_EFI_SIGNATURE_KIND_UNKNOWN,
	FU_EFI_SIGNATURE_KIND_SHA256,
	FU_EFI_SIGNATURE_KIND_X509,
	/*< private >*/
	FU_EFI_SIGNATURE_KIND_LAST
} FuEfiSignatureKind;

#define FU_EFI_SIGNATURE_GUID_ZERO		"00000000-0000-0000-0000-000000000000"
#define FU_EFI_SIGNATURE_GUID_MICROSOFT		"77fa9abd-0359-4d32-bd60-28f4e78f784b"
#define FU_EFI_SIGNATURE_GUID_OVMF		"a0baa8a3-041d-48a8-bc87-c36d121b5e3d"
#define FU_EFI_SIGNATURE_GUID_OVMF_LEGACY	"d5c1df0b-1bac-4edf-ba48-08834009ca5a"

const gchar	*fu_efi_signature_kind_to_string	(FuEfiSignatureKind	 kind);

FuEfiSignatureKind fu_efi_signature_get_kind		(FuEfiSignature		*self);
const gchar 	*fu_efi_signature_get_owner		(FuEfiSignature		*self);
