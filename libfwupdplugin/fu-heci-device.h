/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-heci-struct.h"
#include "fu-mei-device.h"

#define FU_TYPE_HECI_DEVICE (fu_heci_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuHeciDevice, fu_heci_device, FU, HECI_DEVICE, FuMeiDevice)

struct _FuHeciDeviceClass {
	FuMeiDeviceClass parent_class;
};

GByteArray *
fu_heci_device_read_file(FuHeciDevice *self,
			 const gchar *filename,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
GByteArray *
fu_heci_device_read_file_ex(FuHeciDevice *self,
			    guint32 file_id,
			    guint32 section,
			    guint32 datasz_req,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_heci_device_arbh_svn_get_info(FuHeciDevice *self,
				 guint8 usage_id,
				 guint8 *executing,
				 guint8 *min_allowed,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
