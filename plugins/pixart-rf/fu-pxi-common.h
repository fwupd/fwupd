/*
 * Copyright (C) 2021 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

guint8		 fu_pxi_common_sum8		(const guint8	*buf,
						 gsize		 bufsz);
guint16		 fu_pxi_common_sum16		(const guint8	*buf,
						 gsize		 bufsz);
const gchar	*fu_pxi_spec_check_result_to_string (guint8	 spec_check_result);
