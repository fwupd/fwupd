/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>
#include <libfwupd/fwupd-bios-attr.h>

#define FU_TYPE_FIRMWARE_ATTRS (fu_bios_attrs_get_type())

G_DECLARE_FINAL_TYPE(FuBiosAttrs, fu_bios_attrs, FU, BIOS_ATTRS, GObject)

FuBiosAttrs *
fu_bios_attrs_new(void);
gboolean
fu_bios_attrs_get_pending_reboot(FuBiosAttrs *self, gboolean *result, GError **error);
FwupdBiosAttr *
fu_bios_attrs_get_attr(FuBiosAttrs *self, const gchar *val);
