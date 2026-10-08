/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-io-channel.h"
#include "fu-test-bluetooth-proxy.h"

/**
 * FuTestBluetoothProxy:
 *
 * A fake #FuBluetoothProxy used by the self tests, returning canned data instead
 * of talking to a real Bluez device.
 */

struct _FuTestBluetoothProxy {
	FuBluetoothProxy parent_instance;
};

G_DEFINE_TYPE(FuTestBluetoothProxy, fu_test_bluetooth_proxy, FU_TYPE_BLUETOOTH_PROXY)

static GByteArray *
fu_test_bluetooth_proxy_read(FuBluetoothProxy *self, const gchar *uuid, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	const guint8 data[] = {'t', 'e', 's', 't'};
	g_byte_array_append(buf, data, sizeof(data));
	return g_steal_pointer(&buf);
}

static gboolean
fu_test_bluetooth_proxy_write(FuBluetoothProxy *self,
			      const gchar *uuid,
			      GByteArray *buf,
			      GError **error)
{
	return TRUE;
}

static gboolean
fu_test_bluetooth_proxy_notify_start(FuBluetoothProxy *self, const gchar *uuid, GError **error)
{
	return TRUE;
}

static gboolean
fu_test_bluetooth_proxy_notify_stop(FuBluetoothProxy *self, const gchar *uuid, GError **error)
{
	return TRUE;
}

static FuIOChannel *
fu_test_bluetooth_proxy_aquire_notify(FuBluetoothProxy *self,
				      const gchar *uuid,
				      gint32 *mtu,
				      GError **error)
{
	if (mtu != NULL)
		*mtu = 512;
	return fu_io_channel_virtual_new("fwupd-bt-notify", error);
}

static FuIOChannel *
fu_test_bluetooth_proxy_aquire_write(FuBluetoothProxy *self,
				     const gchar *uuid,
				     gint32 *mtu,
				     GError **error)
{
	if (mtu != NULL)
		*mtu = 512;
	return fu_io_channel_virtual_new("fwupd-bt-write", error);
}

static void
fu_test_bluetooth_proxy_init(FuTestBluetoothProxy *self)
{
}

static void
fu_test_bluetooth_proxy_class_init(FuTestBluetoothProxyClass *klass)
{
	FuBluetoothProxyClass *proxy_class = FU_BLUETOOTH_PROXY_CLASS(klass);
	proxy_class->read = fu_test_bluetooth_proxy_read;
	proxy_class->write = fu_test_bluetooth_proxy_write;
	proxy_class->notify_start = fu_test_bluetooth_proxy_notify_start;
	proxy_class->notify_stop = fu_test_bluetooth_proxy_notify_stop;
	proxy_class->aquire_notify = fu_test_bluetooth_proxy_aquire_notify;
	proxy_class->aquire_write = fu_test_bluetooth_proxy_aquire_write;
}

FuTestBluetoothProxy *
fu_test_bluetooth_proxy_new(void)
{
	return g_object_new(FU_TYPE_TEST_BLUETOOTH_PROXY, NULL);
}
