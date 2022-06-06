/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

gchar *
fu_uefi_dbx_get_authenticode_hash(const gchar *fn, GError **error);
gboolean
fu_uefi_dbx_signature_list_validate(FuEfiSignatureList *siglist, GError **error);
