/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_EDID (fu_edid_get_type())

G_DECLARE_FINAL_TYPE(FuEdid, fu_edid, FU, EDID, FuFirmware)

const gchar *
fu_edid_get_pnp_id(FuEdid *self) G_GNUC_NON_NULL(1);
void
fu_edid_set_pnp_id(FuEdid *self, const gchar *pnp_id) G_GNUC_NON_NULL(1);
const gchar *
fu_edid_get_eisa_id(FuEdid *self) G_GNUC_NON_NULL(1);
void
fu_edid_set_eisa_id(FuEdid *self, const gchar *eisa_id) G_GNUC_NON_NULL(1);
const gchar *
fu_edid_get_serial_number(FuEdid *self) G_GNUC_NON_NULL(1);
void
fu_edid_set_serial_number(FuEdid *self, const gchar *serial_number) G_GNUC_NON_NULL(1);
const gchar *
fu_edid_get_product_name(FuEdid *self) G_GNUC_NON_NULL(1);
void
fu_edid_set_product_name(FuEdid *self, const gchar *product_name) G_GNUC_NON_NULL(1);
guint16
fu_edid_get_product_code(FuEdid *self) G_GNUC_NON_NULL(1);
void
fu_edid_set_product_code(FuEdid *self, guint16 product_code) G_GNUC_NON_NULL(1);

FuEdid *
fu_edid_new(void) G_GNUC_WARN_UNUSED_RESULT;
