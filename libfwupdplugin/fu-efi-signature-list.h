/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_SIGNATURE_LIST (fu_efi_signature_list_get_type())
G_DECLARE_DERIVABLE_TYPE(FuEfiSignatureList,
			 fu_efi_signature_list,
			 FU,
			 EFI_SIGNATURE_LIST,
			 FuFirmware)

struct _FuEfiSignatureListClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_efi_signature_list_new(void);
GPtrArray *
fu_efi_signature_list_get_newest(FuEfiSignatureList *self) G_GNUC_NON_NULL(1);
