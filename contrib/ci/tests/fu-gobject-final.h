/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: wrong parent GType
 */

G_DECLARE_FINAL_TYPE(FuSmbios, fu_smbios, FU, SMBIOS, FuFirmware)

/* usually in the .c file... */
G_DEFINE_TYPE(FuSmbios, fu_smbios, FU_TYPE_DEVICE)
