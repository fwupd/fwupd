/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-efi-signature-list.h"

gboolean	 fu_efi_signature_list_inclusive	(FuEfiSignatureList	*outer,
							 FuEfiSignatureList	*inner);
gboolean	 fu_efi_signature_list_has_checksum	(FuEfiSignatureList	*siglist,
							 const gchar		*checksum);
