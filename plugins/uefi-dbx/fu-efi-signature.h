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

const gchar	*fu_efi_signature_kind_to_string	(FuEfiSignatureKind kind);

FuEfiSignature	*fu_efi_signature_new			(FuEfiSignatureKind kind,
							 const gchar	*owner,
							 GBytes		*data);
GBytes		*fu_efi_signature_get_data		(FuEfiSignature	*self);
const gchar	*fu_efi_signature_get_checksum		(FuEfiSignature	*self);
const gchar 	*fu_efi_signature_get_owner		(FuEfiSignature	*self);
