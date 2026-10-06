/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBluetoothDevice"

#include "config.h"

#include "fu-bluetooth-proxy.h"

/**
 * FuBluetoothProxy:
 *
 * A bluetooth device proxy implementation, implemented by BlueZ and FLOSS.
 *
 * See also: [class@FuBluetoothDevice]
 */

G_DEFINE_TYPE(FuBluetoothProxy, fu_bluetooth_proxy, FU_TYPE_DEVICE);

/* private */
GByteArray *
fu_bluetooth_proxy_read(FuBluetoothProxy *self, const gchar *uuid, GError **error)
{
	FuBluetoothProxyClass *klass = FU_BLUETOOTH_PROXY_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_BLUETOOTH_PROXY(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (klass->read == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "->read is not implemented in %s",
			    G_OBJECT_TYPE_NAME(self));
		return NULL;
	}
	return klass->read(self, uuid, error);
}

/* private */
gboolean
fu_bluetooth_proxy_write(FuBluetoothProxy *self, const gchar *uuid, GByteArray *buf, GError **error)
{
	FuBluetoothProxyClass *klass = FU_BLUETOOTH_PROXY_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_BLUETOOTH_PROXY(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);

	if (klass->write == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "->write is not implemented in %s",
			    G_OBJECT_TYPE_NAME(self));
		return FALSE;
	}
	return klass->write(self, uuid, buf, error);
}

/* private */
gboolean
fu_bluetooth_proxy_notify_start(FuBluetoothProxy *self, const gchar *uuid, GError **error)
{
	FuBluetoothProxyClass *klass = FU_BLUETOOTH_PROXY_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_BLUETOOTH_PROXY(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (klass->notify_start == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "->notify_start is not implemented in %s",
			    G_OBJECT_TYPE_NAME(self));
		return FALSE;
	}
	return klass->notify_start(self, uuid, error);
}

/* private */
gboolean
fu_bluetooth_proxy_notify_stop(FuBluetoothProxy *self, const gchar *uuid, GError **error)
{
	FuBluetoothProxyClass *klass = FU_BLUETOOTH_PROXY_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_BLUETOOTH_PROXY(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (klass->notify_stop == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "->notify_stop is not implemented in %s",
			    G_OBJECT_TYPE_NAME(self));
		return FALSE;
	}
	return klass->notify_stop(self, uuid, error);
}

FuIOChannel *
fu_bluetooth_proxy_aquire_notify(FuBluetoothProxy *self,
				 const gchar *uuid,
				 gint32 *mtu,
				 GError **error)
{
	FuBluetoothProxyClass *klass = FU_BLUETOOTH_PROXY_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_BLUETOOTH_PROXY(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (klass->aquire_notify == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "->aquire_notify is not implemented in %s",
			    G_OBJECT_TYPE_NAME(self));
		return NULL;
	}
	return klass->aquire_notify(self, uuid, mtu, error);
}

FuIOChannel *
fu_bluetooth_proxy_aquire_write(FuBluetoothProxy *self,
				const gchar *uuid,
				gint32 *mtu,
				GError **error)
{
	FuBluetoothProxyClass *klass = FU_BLUETOOTH_PROXY_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_BLUETOOTH_PROXY(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (klass->aquire_write == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "->aquire_write is not implemented in %s",
			    G_OBJECT_TYPE_NAME(self));
		return NULL;
	}
	return klass->aquire_write(self, uuid, mtu, error);
}

static void
fu_bluetooth_proxy_init(FuBluetoothProxy *self)
{
}

static void
fu_bluetooth_proxy_class_init(FuBluetoothProxyClass *klass)
{
}
