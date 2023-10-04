/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EDID (fu_edid_get_type())

G_DECLARE_FINAL_TYPE(FuEdid, fu_edid, FU, EDID, FuFirmware)

const gchar *
fu_edid_get_pnp_id(FuEdid *self);
void
fu_edid_set_pnp_id(FuEdid *self, const gchar *pnp_id);
const gchar *
fu_edid_get_eisa_id(FuEdid *self);
void
fu_edid_set_eisa_id(FuEdid *self, const gchar *eisa_id);
const gchar *
fu_edid_get_serial_number(FuEdid *self);
void
fu_edid_set_serial_number(FuEdid *self, const gchar *serial_number);
guint16
fu_edid_get_product_code(FuEdid *self);
void
fu_edid_set_product_code(FuEdid *self, guint16 product_code);

FuEdid *
fu_edid_new(void) G_GNUC_WARN_UNUSED_RESULT;
