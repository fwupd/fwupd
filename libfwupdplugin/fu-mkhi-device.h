/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-mei-device.h"
#include "fu-mkhi-struct.h"

#define FU_TYPE_MKHI_DEVICE (fu_mkhi_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuMkhiDevice, fu_mkhi_device, FU, MKHI_DEVICE, FuMeiDevice)

struct _FuMkhiDeviceClass {
	FuMeiDeviceClass parent_class;
};

GByteArray *
fu_mkhi_device_read_file(FuMkhiDevice *self,
			 const gchar *filename,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
GByteArray *
fu_mkhi_device_read_file_ex(FuMkhiDevice *self,
			    guint32 file_id,
			    guint32 section,
			    guint32 datasz_req,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);

gboolean
fu_mkhi_device_arbh_svn_get_info(FuMkhiDevice *self,
				 guint8 usage_id,
				 guint8 *executing,
				 guint8 *min_allowed,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
