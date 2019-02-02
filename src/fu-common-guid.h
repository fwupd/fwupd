/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_COMMON_GUID_H__
#define __FU_COMMON_GUID_H__

#include <gio/gio.h>

gboolean	 fu_common_guid_is_valid	(const gchar	*guid);
gchar		*fu_common_guid_from_string	(const gchar	*str);
gchar		*fu_common_guid_from_data	(const gchar	*namespace_id,
						 const guint8	*data,
						 gsize		 data_len,
						 GError		**error);
gboolean	 fu_common_guid_is_plausible	(const guint8	*buf);

#endif /* __FU_COMMON_GUID_H__ */
