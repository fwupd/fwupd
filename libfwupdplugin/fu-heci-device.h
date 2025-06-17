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

/**
 * FU_HECI_DEVICE_UUID_MKHI:
 *
 * UUID for MKHI, usually a legacy interface.
 */
#define FU_HECI_DEVICE_UUID_MKHI "8e6a6715-9abc-4043-88ef-9e39c6f63e0f"

/**
 * FU_HECI_DEVICE_UUID_MCHI:
 *
 * UUID for MCHI, commonly called MCA.
 */
#define FU_HECI_DEVICE_UUID_MCHI "dd17041c-09ea-4b17-a271-5b989867ec65"

/**
 * FU_HECI_DEVICE_UUID_MCHI2:
 *
 * Another UUID for MCHI, commonly called MCA.
 */
#define FU_HECI_DEVICE_UUID_MCHI2 "fe2af7a6-ef22-4b45-872f-176b0bbc8b43"

/**
 * FU_HECI_DEVICE_UUID_FWUPDATE:
 *
 * UUID for firmware updates.
 */
#define FU_HECI_DEVICE_UUID_FWUPDATE "87d90ca5-3495-4559-8105-3fbfa37b8b79"

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
