/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-keyring.h"

#define FU_TYPE_KEYRING_PKCS7 (fu_keyring_pkcs7_get_type ())

G_DECLARE_FINAL_TYPE (FuKeyringPkcs7, fu_keyring_pkcs7, FU, KEYRING_PKCS7, FuKeyring)

FuKeyring	*fu_keyring_pkcs7_new		(void);
