/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-lenovo-accessory-struct.h"

#define FU_TYPE_LENOVO_ACCESSORY_IMPL (fu_lenovo_accessory_impl_get_type())
G_DECLARE_INTERFACE(FuLenovoAccessoryImpl,
		    fu_lenovo_accessory_impl,
		    FU,
		    LENOVO_ACCESSORY_IMPL,
		    GObject)

struct _FuLenovoAccessoryImplInterface {
	GTypeInterface g_iface;
	GByteArray *(*read)(FuLenovoAccessoryImpl *self, GError **error);
	gboolean (*write)(FuLenovoAccessoryImpl *self, GByteArray *buf, GError **error);
	GByteArray *(*process)(FuLenovoAccessoryImpl *self, GByteArray *buf, GError **error);
};

gboolean
fu_lenovo_accessory_impl_get_fwversion(FuLenovoAccessoryImpl *self,
				       guint8 *major,
				       guint8 *minor,
				       guint8 *micro,
				       GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_lenovo_accessory_impl_get_mode(FuLenovoAccessoryImpl *self,
				  FuLenovoAccessoryDeviceMode *mode,
				  GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_lenovo_accessory_impl_set_mode(FuLenovoAccessoryImpl *self,
				  FuLenovoAccessoryDeviceMode mode,
				  GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_lenovo_accessory_impl_get_pair_support_info(FuLenovoAccessoryImpl *self,
					       guint8 *max_slot_num,
					       guint8 *slot_status,
					       gsize slot_status_sz,
					       GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_lenovo_accessory_impl_get_pair_slot_info_v2(FuLenovoAccessoryImpl *self,
					       guint8 target_slot,
					       guint16 *pid,
					       guint8 *mac_addr,
					       guint8 mac_addr_sz,
					       gchar **bt_name,
					       GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_lenovo_accessory_impl_dfu_exit(FuLenovoAccessoryImpl *self,
				  FuLenovoAccessoryDfuExitCode exit_code,
				  GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_lenovo_accessory_impl_dfu_attribute(FuLenovoAccessoryImpl *self,
				       guint8 *major_ver,
				       guint8 *minor_ver,
				       guint16 *product_pid,
				       guint8 *processor_id,
				       guint32 *app_max_size,
				       guint32 *page_size,
				       GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_lenovo_accessory_impl_dfu_prepare(FuLenovoAccessoryImpl *self,
				     FuLenovoAccessoryDfuFileType file_type,
				     guint32 start_address,
				     guint32 end_address,
				     guint32 crc32,
				     GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_lenovo_accessory_impl_dfu_file(FuLenovoAccessoryImpl *self,
				  FuLenovoAccessoryDfuFileType file_type,
				  guint32 address,
				  const guint8 *data,
				  gsize datasz,
				  GError **error) G_GNUC_NON_NULL(1, 4);
gboolean
fu_lenovo_accessory_impl_dfu_crc(FuLenovoAccessoryImpl *self, guint32 *crc32, GError **error)
    G_GNUC_NON_NULL(1);
gboolean
fu_lenovo_accessory_impl_dfu_entry(FuLenovoAccessoryImpl *self, GError **error) G_GNUC_NON_NULL(1);
