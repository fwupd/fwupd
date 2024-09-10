/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EFI_LOAD_OPTION (fu_efi_load_option_get_type())

G_DECLARE_FINAL_TYPE(FuEfiLoadOption, fu_efi_load_option, FU, EFI_LOAD_OPTION, FuFirmware)

/**
 * FU_EFI_LOAD_OPTION_METADATA_PATH:
 *
 * The key for the 2nd-stage loader path.
 *
 * Since: 2.0.0
 */
#define FU_EFI_LOAD_OPTION_METADATA_PATH "path"

/**
 * FU_EFI_LOAD_OPTION_METADATA_CMDLINE:
 *
 * The key for the kernel command line.
 *
 * Since: 2.0.0
 */
#define FU_EFI_LOAD_OPTION_METADATA_CMDLINE "cmdline"

const gchar *
fu_efi_load_option_get_metadata(FuEfiLoadOption *self, const gchar *key, GError **error)
    G_GNUC_NON_NULL(1, 2);
void
fu_efi_load_option_set_metadata(FuEfiLoadOption *self, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2);

FuEfiLoadOption *
fu_efi_load_option_new(void) G_GNUC_WARN_UNUSED_RESULT;
