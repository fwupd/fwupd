/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <colorhug.h>
#include <appstream-glib.h>

#include "fu-colorhug-device.h"

typedef struct
{
	ChDeviceQueue		*device_queue;
	gboolean		 is_bootloader;
} FuColorhugDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuColorhugDevice, fu_colorhug_device, FU_TYPE_USB_DEVICE)

#define GET_PRIVATE(o) (fu_colorhug_device_get_instance_private (o))

static void
fu_colorhug_device_finalize (GObject *object)
{
	FuColorhugDevice *device = FU_COLORHUG_DEVICE (object);
	FuColorhugDevicePrivate *priv = GET_PRIVATE (device);

	g_object_unref (priv->device_queue);

	G_OBJECT_CLASS (fu_colorhug_device_parent_class)->finalize (object);
}

static void
fu_colorhug_device_progress_cb (ChDeviceQueue *device_queue,
				guint percentage,
				FuColorhugDevice *device)
{
	fu_device_set_progress (FU_DEVICE (device), percentage);
}

gboolean
fu_colorhug_device_get_is_bootloader (FuColorhugDevice *device)
{
	FuColorhugDevicePrivate *priv = GET_PRIVATE (device);
	return priv->is_bootloader;
}

static gboolean
fu_colorhug_device_detach (FuDevice *device, GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);
	FuColorhugDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(GError) error_local = NULL;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	ch_device_queue_reset (priv->device_queue, usb_device);
	if (!ch_device_queue_process (priv->device_queue,
				      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to reset device: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_colorhug_device_attach (FuDevice *device, GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);
	FuColorhugDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(GError) error_local = NULL;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	ch_device_queue_boot_flash (priv->device_queue, usb_device);
	if (!ch_device_queue_process (priv->device_queue,
				      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to boot to runtime: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_colorhug_device_set_flash_success (FuColorhugDevice *device, GError **error)
{
	FuColorhugDevicePrivate *priv = GET_PRIVATE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(GError) error_local = NULL;

	g_debug ("setting flash success");
	ch_device_queue_set_flash_success (priv->device_queue,
					   usb_device,
					   0x01);
	if (!ch_device_queue_process (priv->device_queue,
				      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to set flash success: %s",
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_colorhug_device_probe (FuUsbDevice *device, GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);
	FuColorhugDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	ChDeviceMode mode;

	/* ignore */
	mode = ch_device_get_mode (usb_device);
	if (mode == CH_DEVICE_MODE_UNKNOWN ||
	    mode == CH_DEVICE_MODE_BOOTLOADER_PLUS ||
	    mode == CH_DEVICE_MODE_FIRMWARE_PLUS) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported with this device");
		return FALSE;
	}

	/* add hardcoded bits */
	fu_device_add_guid (FU_DEVICE (device), ch_device_get_guid (usb_device));
	fu_device_add_icon (FU_DEVICE (device), "colorimeter-colorhug");
	fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_UPDATABLE);

	/* set the display name */
	switch (mode) {
	case CH_DEVICE_MODE_BOOTLOADER:
	case CH_DEVICE_MODE_FIRMWARE:
	case CH_DEVICE_MODE_LEGACY:
		fu_device_set_summary (FU_DEVICE (device),
				       "An open source display colorimeter");
		break;
	case CH_DEVICE_MODE_BOOTLOADER2:
	case CH_DEVICE_MODE_FIRMWARE2:
		fu_device_set_summary (FU_DEVICE (device),
				       "An open source display colorimeter");
		break;
	case CH_DEVICE_MODE_BOOTLOADER_ALS:
	case CH_DEVICE_MODE_FIRMWARE_ALS:
		fu_device_set_summary (FU_DEVICE (device),
				       "An open source ambient light sensor");
		break;
	default:
		break;
	}

	/* is the device in bootloader mode */
	switch (mode) {
	case CH_DEVICE_MODE_BOOTLOADER:
	case CH_DEVICE_MODE_BOOTLOADER2:
	case CH_DEVICE_MODE_BOOTLOADER_ALS:
		priv->is_bootloader = TRUE;
		break;
	default:
		priv->is_bootloader = FALSE;
		break;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_colorhug_device_open (FuUsbDevice *device, GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);
	FuColorhugDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* got the version using the HID API */
	if (!g_usb_device_set_configuration (usb_device, CH_USB_CONFIG, error))
		return FALSE;
	if (!g_usb_device_claim_interface (usb_device, CH_USB_INTERFACE,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		return FALSE;
	}
	if (fu_device_get_version (FU_DEVICE (device)) == NULL) {
		guint16 major;
		guint16 micro;
		guint16 minor;
		g_autofree gchar *version = NULL;
		g_autoptr(GError) error_local = NULL;
		ch_device_queue_get_firmware_ver (priv->device_queue, usb_device,
						  &major, &minor, &micro);
		if (!ch_device_queue_process (priv->device_queue,
					      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
					      NULL, &error_local)) {
			g_warning ("failed to get firmware version: %s",
				   error_local->message);
		}
		version = g_strdup_printf ("%i.%i.%i", major, minor, micro);
		g_debug ("obtained fwver using API '%s'", version);
		fu_device_set_version (FU_DEVICE (device), version);
	}

	/* success */
	return TRUE;
}

gboolean
fu_colorhug_device_verify_firmware (FuColorhugDevice *device, GError **error)
{
	FuColorhugDevicePrivate *priv = GET_PRIVATE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	gsize len;
	g_autoptr(GError) error_local = NULL;
	g_autofree guint8 *data2 = NULL;
	GChecksumType checksum_types[] = {
		G_CHECKSUM_SHA1,
		G_CHECKSUM_SHA256,
		0 };

	/* get the firmware from the device */
	fu_device_set_status (FU_DEVICE (device), FWUPD_STATUS_DEVICE_VERIFY);
	ch_device_queue_read_firmware (priv->device_queue, usb_device,
				       &data2, &len);
	if (!ch_device_queue_process (priv->device_queue,
				      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to dump firmware: %s",
			     error_local->message);
		return FALSE;
	}

	/* get the checksum */
	for (guint i = 0; checksum_types[i] != 0; i++) {
		g_autofree gchar *hash = NULL;
		hash = g_compute_checksum_for_data (checksum_types[i],
						    (guchar *) data2, len);
		fu_device_add_checksum (device, hash);
	}

	return TRUE;
}

static gboolean
fu_colorhug_device_write_firmware (FuDevice *device, GBytes *fw, GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);
	FuColorhugDevicePrivate *priv = GET_PRIVATE (self);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(GError) error_local = NULL;

	/* write firmware */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	ch_device_queue_set_flash_success (priv->device_queue,
					   usb_device,
					   0x00);
	ch_device_queue_write_firmware (priv->device_queue, usb_device,
					g_bytes_get_data (fw, NULL),
					g_bytes_get_size (fw));
	if (!ch_device_queue_process (priv->device_queue,
				      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to write firmware: %s",
			     error_local->message);
		return FALSE;
	}

	/* verify firmware */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);
	ch_device_queue_verify_firmware (priv->device_queue, usb_device,
					 g_bytes_get_data (fw, NULL),
					 g_bytes_get_size (fw));
	if (!ch_device_queue_process (priv->device_queue,
				      CH_DEVICE_QUEUE_PROCESS_FLAGS_NONE,
				      NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to verify firmware: %s",
			     error_local->message);
		return FALSE;
	}

	/* success! */
	return TRUE;
}

static void
fu_colorhug_device_init (FuColorhugDevice *device)
{
	FuColorhugDevicePrivate *priv = GET_PRIVATE (device);
	priv->device_queue = ch_device_queue_new ();
	g_signal_connect (priv->device_queue, "progress_changed",
			  G_CALLBACK (fu_colorhug_device_progress_cb), device);
}

static void
fu_colorhug_device_class_init (FuColorhugDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_colorhug_device_write_firmware;
	klass_device->attach = fu_colorhug_device_attach;
	klass_device->detach = fu_colorhug_device_detach;
	object_class->finalize = fu_colorhug_device_finalize;
	klass_usb_device->open = fu_colorhug_device_open;
	klass_usb_device->probe = fu_colorhug_device_probe;
}

/**
 * fu_colorhug_device_new:
 *
 * Creates a new #FuColorhugDevice.
 *
 * Returns: (transfer full): a #FuColorhugDevice, or %NULL if not a game pad
 *
 * Since: 0.1.0
 **/
FuColorhugDevice *
fu_colorhug_device_new (GUsbDevice *usb_device)
{
	FuColorhugDevice *device = NULL;
	device = g_object_new (FU_TYPE_COLORHUG_DEVICE,
			       "usb-device", usb_device,
			       NULL);
	return device;
}
