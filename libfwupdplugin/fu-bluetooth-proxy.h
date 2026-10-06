/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-bluetooth-device.h"

G_BEGIN_DECLS

#define FU_TYPE_BLUETOOTH_PROXY (fu_bluetooth_proxy_get_type())
G_DECLARE_DERIVABLE_TYPE(FuBluetoothProxy, fu_bluetooth_proxy, FU, BLUETOOTH_PROXY, FuDevice)

struct _FuBluetoothProxyClass {
	FuDeviceClass parent_class;
	GByteArray *(*read)(FuBluetoothProxy *self,
			    const gchar *uuid,
			    GError **error)G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*write)(FuBluetoothProxy *self,
			  const gchar *uuid,
			  GByteArray *buf,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*notify_start)(FuBluetoothProxy *self,
				 const gchar *uuid,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*notify_stop)(FuBluetoothProxy *self,
				const gchar *uuid,
				GError **error) G_GNUC_WARN_UNUSED_RESULT;
	FuIOChannel *(*aquire_notify)(FuBluetoothProxy *self,
				      const gchar *uuid,
				      gint32 *mtu,
				      GError **error)G_GNUC_WARN_UNUSED_RESULT;
	FuIOChannel *(*aquire_write)(FuBluetoothProxy *self,
				     const gchar *uuid,
				     gint32 *mtu,
				     GError **error)G_GNUC_WARN_UNUSED_RESULT;
};

GByteArray *
fu_bluetooth_proxy_read(FuBluetoothProxy *self, const gchar *uuid, GError **error)
    G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_bluetooth_proxy_write(FuBluetoothProxy *self, const gchar *uuid, GByteArray *buf, GError **error)
    G_GNUC_NON_NULL(1, 2, 3) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_bluetooth_proxy_notify_start(FuBluetoothProxy *self, const gchar *uuid, GError **error)
    G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_bluetooth_proxy_notify_stop(FuBluetoothProxy *self, const gchar *uuid, GError **error)
    G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;
FuIOChannel *
fu_bluetooth_proxy_aquire_notify(FuBluetoothProxy *self,
				 const gchar *uuid,
				 gint32 *mtu,
				 GError **error) G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;
FuIOChannel *
fu_bluetooth_proxy_aquire_write(FuBluetoothProxy *self,
				const gchar *uuid,
				gint32 *mtu,
				GError **error) G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS
