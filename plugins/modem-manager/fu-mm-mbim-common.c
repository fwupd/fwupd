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
	GCancellable *cancellable;
	guint timeout_id;
	MbimDevice *mbim_device;
	MbimMessage *mbim_message;
	GError *error;
} _MbimDeviceHelper;

static gboolean
_mbim_device_helper_timeout_cb(gpointer user_data) /* nocheck:name */
{
	_MbimDeviceHelper *helper = (_MbimDeviceHelper *)user_data;
	g_cancellable_cancel(helper->cancellable);
	helper->timeout_id = 0;
	return G_SOURCE_REMOVE;
}

static _MbimDeviceHelper *
_mbim_device_helper_new(guint timeout_ms) /* nocheck:name */
{
	_MbimDeviceHelper *helper = g_new0(_MbimDeviceHelper, 1);
	helper->cancellable = g_cancellable_new();
	helper->loop = g_main_loop_new(NULL, FALSE);
	helper->timeout_id = g_timeout_add(timeout_ms, _mbim_device_helper_timeout_cb, helper);
	return helper;
}

static void
_mbim_device_helper_free(_MbimDeviceHelper *helper) /* nocheck:name */
{
	if (helper->timeout_id != 0)
		g_source_remove(helper->timeout_id);
	g_object_unref(helper->cancellable);
	g_main_loop_unref(helper->loop);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(_MbimDeviceHelper, _mbim_device_helper_free)

static void
_mbim_device_new_cb(GObject *source, GAsyncResult *res, gpointer user_data) /* nocheck:name */
{
	_MbimDeviceHelper *helper = (_MbimDeviceHelper *)user_data;
	helper->mbim_device = mbim_device_new_finish(res, &helper->error);
	g_main_loop_quit(helper->loop);
}

MbimDevice *
_mbim_device_new_sync(GFile *file, guint timeout_ms, GError **error) /* nocheck:name */
{
	g_autoptr(_MbimDeviceHelper) helper = _mbim_device_helper_new(timeout_ms);
	g_return_val_if_fail(G_IS_FILE(file), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	mbim_device_new(file, helper->cancellable, _mbim_device_new_cb, helper);
	g_main_loop_run(helper->loop);
	return helper->mbim_device;
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
		       guint timeout_ms,
		       GError **error)
{
	g_autoptr(_MbimDeviceHelper) helper = _mbim_device_helper_new(timeout_ms);
	g_return_val_if_fail(MBIM_IS_DEVICE(mbim_device), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	mbim_device_open_full(mbim_device,
			      MBIM_DEVICE_OPEN_FLAGS_PROXY,
			      10,
			      helper->cancellable,
			      _mbim_device_open_cb,
			      helper);
	g_main_loop_run(helper->loop);
	return helper->ret;
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
			guint timeout_ms,
			GError **error)
{
	g_autoptr(_MbimDeviceHelper) helper = _mbim_device_helper_new(timeout_ms);
	g_return_val_if_fail(MBIM_IS_DEVICE(mbim_device), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	mbim_device_close(mbim_device, 5, helper->cancellable, _mbim_device_close_cb, helper);
	g_main_loop_run(helper->loop);
	return helper->ret;
}

static void
_mbim_device_command_cb(GObject *source, GAsyncResult *res, gpointer user_data) /* nocheck:name */
{
	_MbimDeviceHelper *helper = (_MbimDeviceHelper *)user_data;
	g_autoptr(MbimMessage) response = NULL;

	response = mbim_device_command_finish(helper->mbim_device, res, &helper->error);
	if (response != NULL) {
		if (mbim_message_response_get_result(response,
						     MBIM_MESSAGE_TYPE_COMMAND_DONE,
						     &helper->error)) {
			helper->mbim_message = g_steal_pointer(&response);
		}
	}
	g_main_loop_quit(helper->loop);
}

MbimMessage *
_mbim_device_command_sync(MbimDevice *mbim_device, /* nocheck:name */
			  MbimMessage *mbim_message,
			  guint timeout_ms,
			  GError **error)
{
	g_autoptr(_MbimDeviceHelper) helper = _mbim_device_helper_new(timeout_ms);
	g_return_val_if_fail(MBIM_IS_DEVICE(mbim_device), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	mbim_device_command(mbim_device,
			    mbim_message,
			    2 * timeout_ms * 1000,
			    helper->cancellable,
			    _mbim_device_command_cb,
			    helper);
	g_main_loop_run(helper->loop);
	return helper->mbim_message;
}
