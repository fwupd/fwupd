/*
 * Copyright 2024 Mike Chang <mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

guint32
fu_telink_dfu_parse_image_version(const gchar *version, FwupdVersionFormat ver_format);
