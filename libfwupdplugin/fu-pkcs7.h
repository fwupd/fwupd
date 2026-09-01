/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

G_BEGIN_DECLS

#define FU_TYPE_PKCS7 (fu_pkcs7_get_type())
G_DECLARE_FINAL_TYPE(FuPkcs7, fu_pkcs7, FU, PKCS7, FuFirmware)

FuPkcs7 *
fu_pkcs7_new(void);

G_END_DECLS
