/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <jcat.h>
#include <xmlb.h>

#include "fwupd-enums.h"

gboolean
fu_keyring_get_release_flags(XbNode *release, FwupdReleaseFlags *flags, GError **error);
