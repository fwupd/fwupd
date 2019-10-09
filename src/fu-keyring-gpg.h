/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-keyring.h"

#define FU_TYPE_KEYRING_GPG (fu_keyring_gpg_get_type ())

G_DECLARE_FINAL_TYPE (FuKeyringGpg, fu_keyring_gpg, FU, KEYRING_GPG, FuKeyring)

FuKeyring	*fu_keyring_gpg_new		(void);
