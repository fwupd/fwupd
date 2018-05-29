/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_KEYRING_PKCS7_H
#define __FU_KEYRING_PKCS7_H

#include "fu-keyring.h"

G_BEGIN_DECLS

#define FU_TYPE_KEYRING_PKCS7 (fu_keyring_pkcs7_get_type ())

G_DECLARE_FINAL_TYPE (FuKeyringPkcs7, fu_keyring_pkcs7, FU, KEYRING_PKCS7, FuKeyring)

FuKeyring	*fu_keyring_pkcs7_new		(void);

G_END_DECLS

#endif /* __FU_KEYRING_PKCS7_H */
