/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-efi-signature-list.h"

typedef enum {
	FU_EFI_SIGNATURE_PARSER_FLAGS_NONE		= 0,
	FU_EFI_SIGNATURE_PARSER_FLAGS_IGNORE_HEADER	= 1 << 0,
} FuEfiSignatureParserFlags;

GPtrArray	*fu_efi_signature_parser_all	(const guint8	*buf,
						 gsize		 bufsz,
						 FuEfiSignatureParserFlags flags,
						 GError		**error);
FuEfiSignatureList* fu_efi_signature_parser_one	(const guint8	*buf,
						 gsize		 bufsz,
						 FuEfiSignatureParserFlags flags,
						 GError		**error);
