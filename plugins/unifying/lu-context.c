/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <gudev/gudev.h>
#include <string.h>

#include "lu-common.h"
#include "lu-context.h"
#include "lu-device-bootloader-nordic.h"
#include "lu-device-bootloader-texas.h"
#include "lu-device-runtime.h"

struct _LuContext
{
	GObject			 parent_instance;
	GPtrArray		*devices;
	GUsbContext		*usb_ctx;
	GUdevClient		*gudev_client;
	GHashTable		*hash_replug;
	gboolean		 done_coldplug;
	GHashTable		*hash_devices;
};

G_DEFINE_TYPE (LuContext, lu_context, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_USB_CONTEXT,
	PROP_LAST
};

enum {
	SIGNAL_ADDED,
	SIGNAL_REMOVED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

typedef struct {
	GMainLoop			*loop;
	LuDevice			*device;
	guint				 timeout_id;
} GUsbContextReplugHelper;

GPtrArray *
lu_context_get_devices (LuContext *ctx)
{
	/* ensure we have devices */
	if (!ctx->done_coldplug)
		lu_context_coldplug (ctx);
	return ctx->devices;
}

static void
lu_device_get_property (GObject *object, guint prop_id,
			GValue *value, GParamSpec *pspec)
{
	LuContext *ctx = LU_CONTEXT (object);
	switch (prop_id) {
	case PROP_USB_CONTEXT:
		g_value_set_object (value, ctx->usb_ctx);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lu_device_set_property (GObject *object, guint prop_id,
			const GValue *value, GParamSpec *pspec)
{
	LuContext *ctx = LU_CONTEXT (object);
	switch (prop_id) {
	case PROP_USB_CONTEXT:
		ctx->usb_ctx = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lu_context_finalize (GObject *object)
{
	LuContext *ctx = LU_CONTEXT (object);
	g_ptr_array_unref (ctx->devices);
	g_object_unref (ctx->usb_ctx);
	g_object_unref (ctx->gudev_client);
	g_hash_table_unref (ctx->hash_devices);
	g_hash_table_unref (ctx->hash_replug);
	G_OBJECT_CLASS (lu_context_parent_class)->finalize (object);
}

static void
lu_context_class_init (LuContextClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = lu_context_finalize;
	object_class->get_property = lu_device_get_property;
	object_class->set_property = lu_device_set_property;

	pspec = g_param_spec_object ("usb-context", NULL, NULL,
				     G_USB_TYPE_CONTEXT,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_USB_CONTEXT, pspec);

	signals [SIGNAL_ADDED] =
		g_signal_new ("added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, LU_TYPE_DEVICE);
	signals [SIGNAL_REMOVED] =
		g_signal_new ("removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, LU_TYPE_DEVICE);
}

static void
lu_context_add_device (LuContext *ctx, LuDevice *device)
{
	GUsbContextReplugHelper *replug_helper;

	g_return_if_fail (LU_IS_CONTEXT (ctx));
	g_return_if_fail (LU_IS_DEVICE (device));

	g_debug ("device %s added", lu_device_get_platform_id (device));

	/* emit */
	g_ptr_array_add (ctx->devices, g_object_ref (device));
	g_signal_emit (ctx, signals[SIGNAL_ADDED], 0, device);

	/* if we're waiting for replug, quit the loop */
	replug_helper = g_hash_table_lookup (ctx->hash_replug,
					     lu_device_get_platform_id (device));
	if (replug_helper != NULL) {
		g_debug ("%s is in replug, quitting loop",
			 lu_device_get_platform_id (device));
		g_main_loop_quit (replug_helper->loop);
	}

}

static void
lu_context_remove_device (LuContext *ctx, LuDevice *device)
{
	g_return_if_fail (LU_IS_CONTEXT (ctx));
	g_return_if_fail (LU_IS_DEVICE (device));

	g_debug ("device %s removed", lu_device_get_platform_id (device));

	/* no longer valid */
	g_object_set (device,
		      "usb-device", NULL,
		      "udev-device", NULL,
		      NULL);

	g_signal_emit (ctx, signals[SIGNAL_REMOVED], 0, device);
	g_ptr_array_remove (ctx->devices, device);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevDevice, g_object_unref)

static const gchar *
lu_context_get_platform_id_for_udev_device (GUdevDevice *udev_device)
{
	g_autoptr(GUdevDevice) udev_device1 = NULL;
	udev_device1 = g_udev_device_get_parent_with_subsystem (udev_device,
							        "usb", "usb_device");
	if (udev_device1 == NULL)
		return NULL;
	return g_udev_device_get_sysfs_path (udev_device1);
}

static void
lu_context_add_udev_device (LuContext *ctx, GUdevDevice *udev_device)
{
	const gchar *val;
	const gchar *platform_id;
	guint16 pid;
	guint16 vid;
	g_autofree gchar *devid = NULL;
	g_autoptr(GUdevDevice) udev_parent = NULL;
	g_autoptr(LuDevice) device = NULL;

	g_return_if_fail (LU_IS_CONTEXT (ctx));

	g_debug ("UDEV add %s = %s",
		 g_udev_device_get_device_file (udev_device),
		 g_udev_device_get_sysfs_path (udev_device));

	/* check the vid:pid from property HID_ID=0003:0000046D:0000C52B */
	udev_parent = g_udev_device_get_parent (udev_device);
	val = g_udev_device_get_property (udev_parent, "HID_ID");
	if (val == NULL) {
		g_debug ("no HID_ID, skipping");
		return;
	}
	if (strlen (val) != 22) {
		g_warning ("property HID_ID invalid '%s', skipping", val);
		return;
	}

	/* is logitech */
	vid = lu_buffer_read_uint16 (val + 10);
	if (vid != LU_DEVICE_VID) {
		g_debug ("not a matching vid: %04x", vid);
		return;
	}

	/* is unifying runtime */
	pid = lu_buffer_read_uint16 (val + 18);
	if (pid == LU_DEVICE_PID_RUNTIME) {
		platform_id = lu_context_get_platform_id_for_udev_device (udev_device);
		device = g_object_new (LU_TYPE_DEVICE_RUNTIME,
				       "kind", LU_DEVICE_KIND_RUNTIME,
				       "platform-id", platform_id,
				       "udev-device", udev_device,
				       NULL);
		g_hash_table_insert (ctx->hash_devices,
				     g_strdup (lu_device_get_platform_id (device)),
				     g_object_ref (device));
		lu_context_add_device (ctx, device);
		return;
	}

	/* is unifying bootloader */
	if (pid == LU_DEVICE_PID_BOOTLOADER_NORDIC ||
	    pid == LU_DEVICE_PID_BOOTLOADER_TEXAS) {
		g_debug ("ignoring bootloader in HID mode");
		return;
	}

	/* is peripheral */
	val = g_udev_device_get_property (udev_parent, "HID_NAME");
	g_debug ("%s not a matching pid: %04x", val, pid);
	platform_id = g_udev_device_get_sysfs_path (udev_device);
	device = g_object_new (LU_TYPE_DEVICE,
			       "kind", LU_DEVICE_KIND_PERIPHERAL,
			       "platform-id", platform_id,
			       "udev-device", udev_device,
			       NULL);
	if (val != NULL) {
		if (g_str_has_prefix (val, "Logitech "))
			val += 9;
		lu_device_set_product (device, val);
	}

	/* generate GUID */
	devid = g_strdup_printf ("USB\\VID_%04X&PID_%04X", vid, pid);
	lu_device_add_guid (device, devid);
	g_hash_table_insert (ctx->hash_devices,
			     g_strdup (lu_device_get_platform_id (device)),
			     g_object_ref (device));
	lu_context_add_device (ctx, device);
}

static gboolean
g_usb_context_replug_timeout_cb (gpointer user_data)
{
	GUsbContextReplugHelper *replug_helper = (GUsbContextReplugHelper *) user_data;
	replug_helper->timeout_id = 0;
	g_main_loop_quit (replug_helper->loop);
	return FALSE;
}

static void
g_usb_context_replug_helper_free (GUsbContextReplugHelper *replug_helper)
{
	if (replug_helper->timeout_id != 0)
		g_source_remove (replug_helper->timeout_id);
	g_main_loop_unref (replug_helper->loop);
	g_object_unref (replug_helper->device);
	g_free (replug_helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUsbContextReplugHelper, g_usb_context_replug_helper_free);

gboolean
lu_context_wait_for_replug (LuContext *ctx,
			    LuDevice *device,
			    guint timeout_ms,
			    GError **error)
{
	g_autoptr(GUsbContextReplugHelper) replug_helper = NULL;
	const gchar *platform_id;

	g_return_val_if_fail (LU_IS_CONTEXT (ctx), FALSE);
	g_return_val_if_fail (LU_IS_DEVICE (device), FALSE);

	/* create a helper */
	replug_helper = g_new0 (GUsbContextReplugHelper, 1);
	replug_helper->device = g_object_ref (device);
	replug_helper->loop = g_main_loop_new (NULL, FALSE);
	replug_helper->timeout_id = g_timeout_add (timeout_ms,
						   g_usb_context_replug_timeout_cb,
						   replug_helper);

	/* register */
	platform_id = lu_device_get_platform_id (device);
	g_hash_table_insert (ctx->hash_replug,
			     g_strdup (platform_id), replug_helper);

	/* wait for timeout, or replug */
	g_main_loop_run (replug_helper->loop);

	/* unregister */
	g_hash_table_remove (ctx->hash_replug, platform_id);

	/* so we timed out; emit the removal now */
	if (replug_helper->timeout_id == 0) {
		g_set_error_literal (error,
				     G_USB_CONTEXT_ERROR,
				     G_USB_CONTEXT_ERROR_INTERNAL,
				     "request timed out");
		return FALSE;
	}
	return TRUE;
}

static void
lu_context_remove_udev_device (LuContext *ctx, GUdevDevice *udev_device)
{
	/* look for this udev_device in all the objects */
	for (guint i = 0; i < ctx->devices->len; i++) {
		LuDevice *device = g_ptr_array_index (ctx->devices, i);
		GUdevDevice *udev_device_tmp = lu_device_get_udev_device (device);
		if (udev_device_tmp == NULL)
			continue;
		if (g_strcmp0 (g_udev_device_get_sysfs_path (udev_device_tmp),
			       g_udev_device_get_sysfs_path (udev_device)) == 0) {
			lu_context_remove_device (ctx, device);
			break;
		}
	}
}

static void
lu_context_udev_uevent_cb (GUdevClient *gudev_client,
			   const gchar *action,
			   GUdevDevice *udev_device,
			   LuContext *ctx)
{
	if (g_strcmp0 (action, "remove") == 0) {
		lu_context_remove_udev_device (ctx, udev_device);
		return;
	}
	if (g_strcmp0 (action, "add") == 0) {
		lu_context_add_udev_device (ctx, udev_device);
		return;
	}
}

static void
lu_context_init (LuContext *ctx)
{
	const gchar *subsystems[] = { "hidraw", NULL };
	ctx->gudev_client = g_udev_client_new (subsystems);
	g_signal_connect (ctx->gudev_client, "uevent",
			  G_CALLBACK (lu_context_udev_uevent_cb), ctx);
	ctx->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	ctx->hash_devices = g_hash_table_new_full (g_str_hash, g_str_equal,
						   g_free, (GDestroyNotify) g_object_unref);
	ctx->hash_replug = g_hash_table_new_full (g_str_hash, g_str_equal,
						  g_free, NULL);
}

void
lu_context_coldplug (LuContext *ctx)
{
	g_autoptr(GList) devices = NULL;

	g_return_if_fail (LU_IS_CONTEXT (ctx));

	if (ctx->done_coldplug)
		return;

	/* coldplug hidraw devices */
	devices = g_udev_client_query_by_subsystem (ctx->gudev_client, "hidraw");
	for (GList *l = devices; l != NULL; l = l->next) {
		GUdevDevice *udev_device = G_UDEV_DEVICE (l->data);
		lu_context_add_udev_device (ctx, udev_device);
		g_object_unref (udev_device);
	}

	/* done */
	ctx->done_coldplug = TRUE;
}

LuDevice *
lu_context_find_by_platform_id (LuContext *ctx, const gchar *platform_id, GError **error)
{
	g_return_val_if_fail (LU_IS_CONTEXT (ctx), NULL);
	g_return_val_if_fail (platform_id != NULL, NULL);

	/* ensure we have devices */
	if (!ctx->done_coldplug)
		lu_context_coldplug (ctx);

	for (guint i = 0; i < ctx->devices->len; i++) {
		LuDevice *device = g_ptr_array_index (ctx->devices, i);
		if (g_strcmp0 (lu_device_get_platform_id (device), platform_id) == 0)
			return device;
	}
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_NOT_FOUND,
		     "not found %s", platform_id);
	return NULL;
}

static void
lu_context_usb_device_added_cb (GUsbContext *usb_ctx,
				GUsbDevice *usb_device,
				LuContext *ctx)
{
	g_return_if_fail (LU_IS_CONTEXT (ctx));

	/* logitech */
	if (g_usb_device_get_vid (usb_device) != LU_DEVICE_VID)
		return;

	g_debug ("USB add %s", g_usb_device_get_platform_id (usb_device));

	/* nordic, in bootloader mode */
	if (g_usb_device_get_pid (usb_device) == LU_DEVICE_PID_BOOTLOADER_NORDIC) {
		g_autoptr(LuDevice) device = NULL;
		device = g_object_new (LU_TYPE_DEVICE_BOOTLOADER_NORDIC,
				       "kind", LU_DEVICE_KIND_BOOTLOADER_NORDIC,
				       "usb-device", usb_device,
				       NULL);
		lu_context_add_device (ctx, device);
		return;
	}

	/* texas, in bootloader mode */
	if (g_usb_device_get_pid (usb_device) == LU_DEVICE_PID_BOOTLOADER_TEXAS) {
		g_autoptr(LuDevice) device = NULL;
		device = g_object_new (LU_TYPE_DEVICE_BOOTLOADER_TEXAS,
				       "kind", LU_DEVICE_KIND_BOOTLOADER_TEXAS,
				       "usb-device", usb_device,
				       NULL);
		lu_context_add_device (ctx, device);
		return;
	}
}

static void
lu_context_usb_device_removed_cb (GUsbContext *usb_ctx,
				  GUsbDevice *usb_device,
				  LuContext *ctx)
{
	g_return_if_fail (LU_IS_CONTEXT (ctx));

	/* logitech */
	if (g_usb_device_get_vid (usb_device) != LU_DEVICE_VID)
		return;

	/* look for this usb_device in all the objects */
	for (guint i = 0; i < ctx->devices->len; i++) {
		LuDevice *device = g_ptr_array_index (ctx->devices, i);
		if (lu_device_get_usb_device (device) == usb_device) {
			lu_context_remove_device (ctx, device);
			break;
		}
	}
}

static void
lu_context_init_real (LuContext *ctx)
{
	g_signal_connect (ctx->usb_ctx, "device-added",
			  G_CALLBACK (lu_context_usb_device_added_cb),
			  ctx);
	g_signal_connect (ctx->usb_ctx, "device-removed",
			  G_CALLBACK (lu_context_usb_device_removed_cb),
			  ctx);
}

LuContext *
lu_context_new (GError **error)
{
	LuContext *ctx = NULL;
	g_autoptr(GUsbContext) usb_ctx = NULL;

	usb_ctx = g_usb_context_new (error);
	if (usb_ctx == NULL)
		return NULL;
	ctx = g_object_new (LU_TYPE_CONTEXT,
			    "usb-context", usb_ctx,
			    NULL);
	lu_context_init_real (ctx);
	g_usb_context_enumerate (ctx->usb_ctx);
	return ctx;
}

LuContext *
lu_context_new_full (GUsbContext *usb_ctx)
{
	LuContext *ctx = NULL;
	ctx = g_object_new (LU_TYPE_CONTEXT,
			    "usb-context", usb_ctx,
			    NULL);
	lu_context_init_real (ctx);
	return ctx;
}
