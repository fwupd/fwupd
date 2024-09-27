/*
 * Copyright 2024 Maciej Borzecki <maciej.borzecki@canonical.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SNAPD_OBSERVER (fu_snapd_observer_get_type())
G_DECLARE_FINAL_TYPE(FuSnapdObserver, fu_snapd_observer, FU, SNAPD_OBSERVER, GObject)

FuSnapdObserver *
fu_snapd_observer_new(void);

gboolean
fu_snapd_observer_notify_secureboot_manager_startup(FuSnapdObserver *self, GError **error);

gboolean
fu_snapd_observer_notify_secureboot_dbx_update_prepare(FuSnapdObserver *self,
						       GBytes *fw_payload,
						       GError **error);

gboolean
fu_snapd_observer_notify_secureboot_db_update_cleanup(FuSnapdObserver *self, GError **error);
