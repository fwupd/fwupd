/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mm-mbim-common.h"

typedef struct {
	gboolean ret;
	GMainLoop *loop;
	MbimDevice *mbim_device;
	GError *error;
} _MbimDeviceHelper;

static void
_mbim_device_new_cb(GObject *source, GAsyncResult *res, gpointer user_data) /* nocheck:name */
{
	_MbimDeviceHelper *helper = (_MbimDeviceHelper *)user_data;
	helper->mbim_device = mbim_device_new_finish(res, &helper->error);
	g_main_loop_quit(helper->loop);
}

MbimDevice *
_mbim_device_new_sync(GFile *file, GCancellable *cancellable, GError **error) /* nocheck:name */
{
	g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
	_MbimDeviceHelper helper = {.loop = g_main_loop_new(NULL, FALSE)};
	mbim_device_new(file, cancellable, _mbim_device_new_cb, &helper);
	g_main_loop_run(loop);
	return helper.mbim_device;
}

static void
_mbim_device_open_cb(GObject *source, GAsyncResult *res, gpointer user_data) /* nocheck:name */
{
	_MbimDeviceHelper *helper = (_MbimDeviceHelper *)user_data;
	helper->ret = mbim_device_open_full_finish(helper->mbim_device, res, &helper->error);
	g_main_loop_quit(helper->loop);
}

gboolean
_mbim_device_open_sync(MbimDevice *mbim_device, /* nocheck:name */
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
	_MbimDeviceHelper helper = {.loop = g_main_loop_new(NULL, FALSE)};
	mbim_device_open_full(mbim_device,
			      MBIM_DEVICE_OPEN_FLAGS_PROXY,
			      10,
			      cancellable,
			      _mbim_device_open_cb,
			      &helper);
	g_main_loop_run(loop);
	return helper.ret;
}

static void
_mbim_device_close_cb(GObject *source, GAsyncResult *res, gpointer user_data) /* nocheck:name */
{
	_MbimDeviceHelper *helper = (_MbimDeviceHelper *)user_data;
	helper->ret = mbim_device_close_finish(helper->mbim_device, res, &helper->error);
	g_main_loop_quit(helper->loop);
}

gboolean
_mbim_device_close_sync(MbimDevice *mbim_device, /* nocheck:name */
			GCancellable *cancellable,
			GError **error)
{
	g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
	_MbimDeviceHelper helper = {.loop = g_main_loop_new(NULL, FALSE)};
	mbim_device_close(mbim_device, 5, cancellable, _mbim_device_close_cb, &helper);
	g_main_loop_run(loop);
	return helper.ret;
}
