/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: native device functions should use self as the first parameter not device
 */

static gboolean
fu_param_self_native_recv(FuHughskiColorhugDevice *self, GError **error)
{
}

static gboolean
fu_param_self_native_send(FuDevice *device, GError **error)
{
}

/* allowed to be FuDevice... */
static gboolean
fu_param_self_native_attach(FuDevice *device, GError **error)
{
	FuHughskiColorhugDevice *self = FU_HUGHSKI_COLORHUG_DEVICE(device);
	return fu_param_self_native_send(FU_DEVICE(self), error);
}

static void
fu_param_self_native_class_init(FuHughskiColorhugDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->attach = fu_param_self_native_attach;
}
