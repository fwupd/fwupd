/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#define FU_TYPE_EFI_SIGNATURE (fu_efi_signature_get_type ())
G_DECLARE_FINAL_TYPE (FuEfiSignature, fu_efi_signature, FU, EFI_SIGNATURE, GObject)

typedef enum {
	FU_EFI_SIGNATURE_KIND_UNKNOWN,
	FU_EFI_SIGNATURE_KIND_SHA256,
	FU_EFI_SIGNATURE_KIND_X509,
	FU_EFI_SIGNATURE_KIND_LAST
} FuEfiSignatureKind;

#define FU_EFI_SIGNATURE_GUID_ZERO		"00000000-0000-0000-0000-000000000000"
#define FU_EFI_SIGNATURE_GUID_MICROSOFT		"77fa9abd-0359-4d32-bd60-28f4e78f784b"
#define FU_EFI_SIGNATURE_GUID_OVMF		"a0baa8a3-041d-48a8-bc87-c36d121b5e3d"

const gchar	*fu_efi_signature_kind_to_string	(FuEfiSignatureKind kind);
const gchar	*fu_efi_signature_guid_to_string	(const gchar	*guid);

FuEfiSignature	*fu_efi_signature_new			(FuEfiSignatureKind kind,
							 const gchar	*owner,
							 GBytes		*data);
FuEfiSignatureKind fu_efi_signature_get_kind		(FuEfiSignature	*self);
GBytes		*fu_efi_signature_get_data		(FuEfiSignature	*self);
const gchar	*fu_efi_signature_get_checksum		(FuEfiSignature	*self);
const gchar 	*fu_efi_signature_get_owner		(FuEfiSignature	*self);
