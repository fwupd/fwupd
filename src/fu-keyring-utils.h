/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <xmlb.h>

#include "fu-keyring.h"
#include "fwupd-enums.h"

FuKeyring	*fu_keyring_create_for_kind		(FwupdKeyringKind kind,
							 GError		**error);
gboolean	 fu_keyring_get_release_flags		(XbNode		*release,
							 FwupdReleaseFlags *flags,
							 GError		**error);
