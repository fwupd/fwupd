/*
 * Copyright 2024 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-focal-moc-struct.h"

#define FU_TYPE_FOCAL_MOC_DEVICE (fu_focal_moc_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuFocalMocDevice, fu_focal_moc_device, FU, FOCAL_MOC_DEVICE, FuUsbDevice)

struct _FuFocalMocDeviceClass {
	FuUsbDeviceClass parent_class;
	GByteArray *(*command)(FuFocalMocDevice *self,
			       guint8 command,
			       const guint8 *payload,
			       gsize payload_sz,
			       guint timeout_ms,
			       guint8 *status,
			       GError **error)G_GNUC_NON_NULL(1);
	GByteArray *(*build_soh)(FuFocalMocDevice *self,
				 gsize firmware_sz,
				 guint32 crc32,
				 GError **error)G_GNUC_NON_NULL(1);
	GByteArray *(*build_data)(FuFocalMocDevice *self,
				  FuFocalMocFrame kind,
				  guint16 sequence,
				  const guint8 *buf,
				  gsize bufsz,
				  GError **error)G_GNUC_NON_NULL(1);
	FuInputStream *(*get_firmware_stream)(FuFocalMocDevice *self,
					      FuFirmware *firmware,
					      GError **error)G_GNUC_NON_NULL(1, 2);
	gboolean (*ensure_bootloader_layout)(FuFocalMocDevice *self, GError **error)
	    G_GNUC_NON_NULL(1);
};

/* the reply carries one byte of stale buffer past the declared frame */
#define FU_FOCAL_MOC_DEVICE_FLAG_LEGACY_TRAILER "legacy-trailer"

gboolean
fu_focal_moc_device_send_raw(FuFocalMocDevice *self,
			     const guint8 *buf,
			     gsize bufsz,
			     guint timeout_ms,
			     GError **error) G_GNUC_NON_NULL(1, 2);

GByteArray *
fu_focal_moc_device_receive_raw(FuFocalMocDevice *self, guint timeout_ms, GError **error)
    G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;

void
fu_focal_moc_device_set_iap_status_layout(FuFocalMocDevice *self,
					  FuFocalMocIapStatusLayout iap_status_layout)
    G_GNUC_NON_NULL(1);
