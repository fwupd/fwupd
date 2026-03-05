/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>
#include "fu-pixart-tp-struct.h"

#define FU_TYPE_PIXART_TP_DEVICE (fu_pixart_tp_device_get_type())

G_DECLARE_DERIVABLE_TYPE(FuPixartTpDevice,
			 fu_pixart_tp_device,
			 FU,
			 PIXART_TP_DEVICE,
			 FuHidrawDevice)

struct _FuPixartTpDeviceClass {
	FuHidrawDeviceClass parent_class;
};

gboolean
fu_pixart_tp_device_register_write(FuPixartTpDevice *self,
				   FuPixartTpSystemBank bank,
				   guint8 addr,
				   guint8 val,
				   GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_pixart_tp_device_register_write_uint16(FuPixartTpDevice *self,
					  FuPixartTpSystemBank bank,
					  guint8 addr,
					  guint16 val,
					  GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_pixart_tp_device_register_write_uint24(FuPixartTpDevice *self,
					  FuPixartTpSystemBank bank,
					  guint8 addr,
					  guint32 val,
					  GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_pixart_tp_device_register_write_uint32(FuPixartTpDevice *self,
					  FuPixartTpSystemBank bank,
					  guint8 addr,
					  guint32 val,
					  GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_pixart_tp_device_register_read(FuPixartTpDevice *self,
				  FuPixartTpSystemBank bank,
				  guint8 addr,
				  guint8 *out_val,
				  GError **error) G_GNUC_NON_NULL(1, 4);

GByteArray *
fu_pixart_tp_device_register_read_array(FuPixartTpDevice *self,
					FuPixartTpSystemBank bank,
					guint8 addr,
					gsize len,
					GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_pixart_tp_device_register_user_write(FuPixartTpDevice *self,
					FuPixartTpUserBank bank,
					guint8 addr,
					guint8 val,
					GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_pixart_tp_device_register_burst_write(FuPixartTpDevice *self,
					 const guint8 *buf,
					 gsize bufsz,
					 GError **error) G_GNUC_NON_NULL(1, 2);

gboolean
fu_pixart_tp_device_register_burst_read(FuPixartTpDevice *self,
					guint8 *buf,
					gsize bufsz,
					GError **error) G_GNUC_NON_NULL(1, 2);
