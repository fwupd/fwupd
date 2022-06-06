/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include <jcat.h>

gboolean
fu_keyring_get_release_flags(XbNode *release, FwupdReleaseFlags *flags, GError **error);
