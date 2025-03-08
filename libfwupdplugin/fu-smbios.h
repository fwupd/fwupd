/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_SMBIOS (fu_smbios_get_type())

G_DECLARE_FINAL_TYPE(FuSmbios, fu_smbios, FU, SMBIOS, FuFirmware)

/**
 * FU_SMBIOS_STRUCTURE_LENGTH_ANY:
 *
 * Accept any structure length.
 *
 * Since: 2.0.7
 **/
#define FU_SMBIOS_STRUCTURE_LENGTH_ANY G_MAXUINT8

FuSmbios *
fu_smbios_new(void);

const gchar *
fu_smbios_get_string(FuSmbios *self, guint8 type, guint8 length, guint8 offset, GError **error)
    G_GNUC_NON_NULL(1);
guint
fu_smbios_get_integer(FuSmbios *self, guint8 type, guint8 length, guint8 offset, GError **error)
    G_GNUC_NON_NULL(1);
GPtrArray *
fu_smbios_get_data(FuSmbios *self, guint8 type, guint8 length, GError **error) G_GNUC_NON_NULL(1);
