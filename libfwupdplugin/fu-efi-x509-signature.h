/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-efi-signature.h"

#define FU_TYPE_EFI_X509_SIGNATURE (fu_efi_x509_signature_get_type())
G_DECLARE_FINAL_TYPE(FuEfiX509Signature,
		     fu_efi_x509_signature,
		     FU,
		     EFI_X509_SIGNATURE,
		     FuEfiSignature)

const gchar *
fu_efi_x509_signature_get_issuer(FuEfiX509Signature *self) G_GNUC_NON_NULL(1);
const gchar *
fu_efi_x509_signature_get_subject(FuEfiX509Signature *self) G_GNUC_NON_NULL(1);
const gchar *
fu_efi_x509_signature_get_subject_name(FuEfiX509Signature *self) G_GNUC_NON_NULL(1);
const gchar *
fu_efi_x509_signature_get_subject_vendor(FuEfiX509Signature *self) G_GNUC_NON_NULL(1);
