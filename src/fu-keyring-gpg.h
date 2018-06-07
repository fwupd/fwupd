/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_KEYRING_GPG_H
#define __FU_KEYRING_GPG_H

#include "fu-keyring.h"

G_BEGIN_DECLS

#define FU_TYPE_KEYRING_GPG (fu_keyring_gpg_get_type ())

G_DECLARE_FINAL_TYPE (FuKeyringGpg, fu_keyring_gpg, FU, KEYRING_GPG, FuKeyring)

FuKeyring	*fu_keyring_gpg_new		(void);

G_END_DECLS

#endif /* __FU_KEYRING_GPG_H */
