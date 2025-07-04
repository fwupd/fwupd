/*
 * Copyright 2024 Maciej Borzecki <maciej.borzecki@canonical.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_UEFI_DBX_SNAPD_NOTIFIER (fu_uefi_dbx_snapd_notifier_get_type())
G_DECLARE_FINAL_TYPE(FuUefiDbxSnapdNotifier,
		     fu_uefi_dbx_snapd_notifier,
		     FU,
		     UEFI_DBX_SNAPD_NOTIFIER,
		     GObject)

FuUefiDbxSnapdNotifier *
fu_uefi_dbx_snapd_notifier_new(void);

gboolean
fu_uefi_dbx_snapd_notifier_dbx_manager_startup(FuUefiDbxSnapdNotifier *self, GError **error);

gboolean
fu_uefi_dbx_snapd_notifier_dbx_update_prepare(FuUefiDbxSnapdNotifier *self,
					      GBytes *fw_payload,
					      GError **error);

gboolean
fu_uefi_dbx_snapd_notifier_dbx_update_cleanup(FuUefiDbxSnapdNotifier *self, GError **error);
