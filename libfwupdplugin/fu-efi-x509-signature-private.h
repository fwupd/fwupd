/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-efi-x509-signature.h"

FuEfiX509Signature *
fu_efi_x509_signature_new(void);
void
fu_efi_x509_signature_set_issuer(FuEfiX509Signature *self, const gchar *issuer) G_GNUC_NON_NULL(1);
void
fu_efi_x509_signature_set_subject(FuEfiX509Signature *self, const gchar *subject)
    G_GNUC_NON_NULL(1);
