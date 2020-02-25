/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <xmlb.h>
#include <jcat.h>

#include "fwupd-enums.h"

gboolean	 fu_keyring_get_release_flags		(XbNode		*release,
							 FwupdReleaseFlags *flags,
							 GError		**error);
