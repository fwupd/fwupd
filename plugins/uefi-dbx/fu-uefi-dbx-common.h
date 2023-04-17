/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

gboolean
fu_uefi_dbx_signature_list_validate(FuContext *ctx, FuEfiSignatureList *siglist, GError **error);
