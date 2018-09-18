/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_KEYRING_UTILS_H__
#define __FU_KEYRING_UTILS_H__

#include "xb-node.h"

#include "fu-keyring.h"
#include "fwupd-enums.h"

FuKeyring	*fu_keyring_create_for_kind		(FwupdKeyringKind kind,
							 GError		**error);
gboolean	 fu_keyring_get_release_trust_flags	(XbNode		*release,
							 FwupdTrustFlags *trust_flags,
							 GError		**error);

#endif /* __FU_KEYRING_UTILS_H__ */
