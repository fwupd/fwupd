/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-efi-signature.h"

FuEfiSignature	*fu_efi_signature_new			(FuEfiSignatureKind kind,
							 const gchar	*owner);
