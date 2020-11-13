/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupd.h>

gchar		*fu_security_attr_get_name	(FwupdSecurityAttr	*attr);
const gchar	*fu_security_attr_get_result	(FwupdSecurityAttr	*attr);
