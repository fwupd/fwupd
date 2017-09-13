/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __FWUPD_RESULT_H
#define __FWUPD_RESULT_H

#include <glib-object.h>

#include "fwupd-device.h"
#include "fwupd-enums.h"
#include "fwupd-release.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_RESULT (fwupd_result_get_type ())
G_DECLARE_DERIVABLE_TYPE (FwupdResult, fwupd_result, FWUPD, RESULT, GObject)

struct _FwupdResultClass
{
	GObjectClass			 parent_class;
	/*< private >*/
	void (*_fwupd_reserved1)	(void);
	void (*_fwupd_reserved2)	(void);
	void (*_fwupd_reserved3)	(void);
	void (*_fwupd_reserved4)	(void);
	void (*_fwupd_reserved5)	(void);
	void (*_fwupd_reserved6)	(void);
	void (*_fwupd_reserved7)	(void);
};

FwupdResult	*fwupd_result_new			(void);
FwupdResult	*fwupd_result_new_from_data		(GVariant	*data);
const gchar	*fwupd_result_get_unique_id		(FwupdResult	*result);
void		 fwupd_result_set_unique_id		(FwupdResult	*result,
							 const gchar	*unique_id);
FwupdRelease	*fwupd_result_get_release		(FwupdResult	*result);
FwupdDevice	*fwupd_result_get_device		(FwupdResult	*result);

FwupdTrustFlags	 fwupd_result_get_update_trust_flags	(FwupdResult	*result);
void		 fwupd_result_set_update_trust_flags	(FwupdResult	*result,
							 FwupdTrustFlags trust_flags);

GVariant	*fwupd_result_to_data			(FwupdResult	*result,
							 const gchar	*type_string);
gchar		*fwupd_result_to_string			(FwupdResult	*result);

G_END_DECLS

#endif /* __FWUPD_RESULT_H */

