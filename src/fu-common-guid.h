/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_COMMON_GUID_H__
#define __FU_COMMON_GUID_H__

#include <gio/gio.h>

gboolean	 fu_common_guid_is_plausible	(const guint8	*buf);

#endif /* __FU_COMMON_GUID_H__ */
