/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

const gchar *
fu_uefi_dbx_get_efi_arch(void);
gboolean
fu_uefi_dbx_signature_list_validate(FuContext *ctx,
				    FuEfiSignatureList *siglist,
				    FwupdInstallFlags flags,
				    GError **error);
