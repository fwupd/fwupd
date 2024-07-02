/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#ifdef HAVE_GUSB
#include <gusb.h>
#else
#define GUsbContext		      GObject
#define GUsbDevice		      GObject
#define GUsbInterface		      GObject
#define GUsbDeviceDirection	      gint
#define GUsbDeviceRequestType	      gint
#define GUsbDeviceRecipient	      gint
#define GUsbDeviceClaimInterfaceFlags gint
#ifndef __GI_SCANNER__
#define G_USB_CHECK_VERSION(a, c, b) 0
#endif
#endif

#include "fu-plugin.h"
#include "fu-udev-device.h"

#define FU_TYPE_USB_DEVICE (fu_usb_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUsbDevice, fu_usb_device, FU, USB_DEVICE, FuDevice)

struct _FuUsbDeviceClass {
	FuDeviceClass parent_class;
};

FuUsbDevice *
fu_usb_device_get_parent(FuUsbDevice *self) G_GNUC_NON_NULL(1);

guint16
fu_usb_device_get_vid(FuUsbDevice *self) G_GNUC_NON_NULL(1);
guint16
fu_usb_device_get_pid(FuUsbDevice *self) G_GNUC_NON_NULL(1);
guint16
fu_usb_device_get_release(FuUsbDevice *self) G_GNUC_NON_NULL(1);
guint16
fu_usb_device_get_spec(FuUsbDevice *self) G_GNUC_NON_NULL(1);
guint8
fu_usb_device_get_device_class(FuUsbDevice *self);

guint8
fu_usb_device_get_configuration_index(FuUsbDevice *self);
guint8
fu_usb_device_get_serial_number_index(FuUsbDevice *self);
guint8
fu_usb_device_get_custom_index(FuUsbDevice *self,
			       guint8 class_id,
			       guint8 subclass_id,
			       guint8 protocol_id,
			       GError **error);

FuDevice *
fu_usb_device_find_udev_device(FuUsbDevice *device, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
void
fu_usb_device_set_configuration(FuUsbDevice *device, gint configuration) G_GNUC_NON_NULL(1);
void
fu_usb_device_add_interface(FuUsbDevice *device, guint8 number) G_GNUC_NON_NULL(1);
void
fu_usb_device_set_claim_retry_count(FuUsbDevice *self, guint claim_retry_count) G_GNUC_NON_NULL(1);
guint
fu_usb_device_get_claim_retry_count(FuUsbDevice *self) G_GNUC_NON_NULL(1);
void
fu_usb_device_set_open_retry_count(FuUsbDevice *self, guint open_retry_count) G_GNUC_NON_NULL(1);
guint
fu_usb_device_get_open_retry_count(FuUsbDevice *self) G_GNUC_NON_NULL(1);

gboolean
fu_usb_device_control_transfer(FuUsbDevice *self,
			       GUsbDeviceDirection direction,
			       GUsbDeviceRequestType request_type,
			       GUsbDeviceRecipient recipient,
			       guint8 request,
			       guint16 value,
			       guint16 idx,
			       guint8 *data,
			       gsize length,
			       gsize *actual_length,
			       guint timeout,
			       GCancellable *cancellable,
			       GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_usb_device_bulk_transfer(FuUsbDevice *self,
			    guint8 endpoint,
			    guint8 *data,
			    gsize length,
			    gsize *actual_length,
			    guint timeout,
			    GCancellable *cancellable,
			    GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_usb_device_interrupt_transfer(FuUsbDevice *self,
				 guint8 endpoint,
				 guint8 *data,
				 gsize length,
				 gsize *actual_length,
				 guint timeout,
				 GCancellable *cancellable,
				 GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_usb_device_claim_interface(FuUsbDevice *self,
			      guint8 iface,
			      GUsbDeviceClaimInterfaceFlags flags,
			      GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_usb_device_release_interface(FuUsbDevice *self,
				guint8 iface,
				GUsbDeviceClaimInterfaceFlags flags,
				GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_usb_device_reset(FuUsbDevice *self, GError **error) G_GNUC_NON_NULL(1);
GPtrArray *
fu_usb_device_get_interfaces(FuUsbDevice *self, GError **error) G_GNUC_NON_NULL(1);
GUsbInterface *
fu_usb_device_get_interface(FuUsbDevice *self,
			    guint8 class_id,
			    guint8 subclass_id,
			    guint8 protocol_id,
			    GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_usb_device_set_interface_alt(FuUsbDevice *self, guint8 iface, guint8 alt, GError **error)
    G_GNUC_NON_NULL(1);
gchar *
fu_usb_device_get_string_descriptor(FuUsbDevice *self, guint8 desc_index, GError **error)
    G_GNUC_NON_NULL(1);
GBytes *
fu_usb_device_get_string_descriptor_bytes(FuUsbDevice *self,
					  guint8 desc_index,
					  guint16 langid,
					  GError **error) G_GNUC_NON_NULL(1);
GBytes *
fu_usb_device_get_string_descriptor_bytes_full(FuUsbDevice *self,
					       guint8 desc_index,
					       guint16 langid,
					       gsize length,
					       GError **error) G_GNUC_NON_NULL(1);
GPtrArray *
fu_usb_device_get_hid_descriptors(FuUsbDevice *self, GError **error) G_GNUC_NON_NULL(1);
