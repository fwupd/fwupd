/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-efi-signature.h"

G_BEGIN_DECLS

FuEfiSignature *
fu_efi_signature_new(FuEfiSignatureKind kind);
void
fu_efi_signature_set_kind(FuEfiSignature *self, FuEfiSignatureKind kind);
void
fu_efi_signature_set_owner(FuEfiSignature *self, const gchar *owner) G_GNUC_NON_NULL(1);

G_END_DECLS
