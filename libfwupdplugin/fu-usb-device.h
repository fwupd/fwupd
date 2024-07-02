/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-plugin.h"
#include "fu-udev-device.h"
#include "fu-usb-interface.h"

#define FU_TYPE_USB_DEVICE (fu_usb_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUsbDevice, fu_usb_device, FU, USB_DEVICE, FuDevice)

struct _FuUsbDeviceClass {
	FuDeviceClass parent_class;
};

/**
 * FuUsbDeviceDirection:
 *
 * The message direction.
 **/
typedef enum {
	FU_USB_DEVICE_DIRECTION_DEVICE_TO_HOST, /* IN */
	FU_USB_DEVICE_DIRECTION_HOST_TO_DEVICE	/* OUT */
} FuUsbDeviceDirection;

/**
 * FuUsbDeviceRequestType:
 *
 * The message request type.
 **/
typedef enum {
	FU_USB_DEVICE_REQUEST_TYPE_STANDARD,
	FU_USB_DEVICE_REQUEST_TYPE_CLASS,
	FU_USB_DEVICE_REQUEST_TYPE_VENDOR,
	FU_USB_DEVICE_REQUEST_TYPE_RESERVED
} FuUsbDeviceRequestType;

/**
 * FuUsbDeviceRecipient:
 *
 * The message recipient.
 **/
typedef enum {
	FU_USB_DEVICE_RECIPIENT_DEVICE,
	FU_USB_DEVICE_RECIPIENT_INTERFACE,
	FU_USB_DEVICE_RECIPIENT_ENDPOINT,
	FU_USB_DEVICE_RECIPIENT_OTHER
} FuUsbDeviceRecipient;

/**
 * FuUsbDeviceError:
 * @FU_USB_DEVICE_ERROR_INTERNAL:		Internal error
 * @FU_USB_DEVICE_ERROR_IO:			IO error
 * @FU_USB_DEVICE_ERROR_TIMED_OUT:		Operation timed out
 * @FU_USB_DEVICE_ERROR_NOT_SUPPORTED:		Operation not supported
 * @FU_USB_DEVICE_ERROR_NO_DEVICE:		No device found
 * @FU_USB_DEVICE_ERROR_NOT_OPEN:		Device is not open
 * @FU_USB_DEVICE_ERROR_ALREADY_OPEN:		Device is already open
 * @FU_USB_DEVICE_ERROR_CANCELLED:		Operation was cancelled
 * @FU_USB_DEVICE_ERROR_FAILED:			Operation failed
 * @FU_USB_DEVICE_ERROR_PERMISSION_DENIED:	Permission denied
 * @FU_USB_DEVICE_ERROR_BUSY:			Device was busy
 *
 * The error code.
 **/
typedef enum {
	FU_USB_DEVICE_ERROR_INTERNAL,
	FU_USB_DEVICE_ERROR_IO,
	FU_USB_DEVICE_ERROR_TIMED_OUT,
	FU_USB_DEVICE_ERROR_NOT_SUPPORTED,
	FU_USB_DEVICE_ERROR_NO_DEVICE,
	FU_USB_DEVICE_ERROR_NOT_OPEN,
	FU_USB_DEVICE_ERROR_ALREADY_OPEN,
	FU_USB_DEVICE_ERROR_CANCELLED,
	FU_USB_DEVICE_ERROR_FAILED,
	FU_USB_DEVICE_ERROR_PERMISSION_DENIED,
	FU_USB_DEVICE_ERROR_BUSY,
	/*< private >*/
	FU_USB_DEVICE_ERROR_LAST
} FuUsbDeviceError;

/**
 * FuUsbDeviceClaimInterfaceFlags:
 *
 * Flags for the fu_usb_device_claim_interface and
 * fu_usb_device_release_interface methods flags parameters.
 **/
typedef enum {
	FU_USB_DEVICE_CLAIM_INTERFACE_NONE = 0,
	FU_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER = 1 << 0,
} FuUsbDeviceClaimInterfaceFlags;

/**
 * FuUsbDeviceClassCode:
 *
 * The USB device class.
 **/
typedef enum {
	FU_USB_DEVICE_CLASS_INTERFACE_DESC = 0x00,
	FU_USB_DEVICE_CLASS_AUDIO = 0x01,
	FU_USB_DEVICE_CLASS_COMMUNICATIONS = 0x02,
	FU_USB_DEVICE_CLASS_HID = 0x03,
	FU_USB_DEVICE_CLASS_PHYSICAL = 0x05,
	FU_USB_DEVICE_CLASS_IMAGE = 0x06,
	FU_USB_DEVICE_CLASS_PRINTER = 0x07,
	FU_USB_DEVICE_CLASS_MASS_STORAGE = 0x08,
	FU_USB_DEVICE_CLASS_HUB = 0x09,
	FU_USB_DEVICE_CLASS_CDC_DATA = 0x0a,
	FU_USB_DEVICE_CLASS_SMART_CARD = 0x0b,
	FU_USB_DEVICE_CLASS_CONTENT_SECURITY = 0x0d,
	FU_USB_DEVICE_CLASS_VIDEO = 0x0e,
	FU_USB_DEVICE_CLASS_PERSONAL_HEALTHCARE = 0x0f,
	FU_USB_DEVICE_CLASS_AUDIO_VIDEO = 0x10,
	FU_USB_DEVICE_CLASS_BILLBOARD = 0x11,
	FU_USB_DEVICE_CLASS_DIAGNOSTIC = 0xdc,
	FU_USB_DEVICE_CLASS_WIRELESS_CONTROLLER = 0xe0,
	FU_USB_DEVICE_CLASS_MISCELLANEOUS = 0xef,
	FU_USB_DEVICE_CLASS_APPLICATION_SPECIFIC = 0xfe,
	FU_USB_DEVICE_CLASS_VENDOR_SPECIFIC = 0xff
} FuUsbDeviceClassCode;

/**
 * FuUsbDeviceLangid:
 *
 * The USB language ID.
 **/
typedef enum {
	FU_USB_DEVICE_LANGID_INVALID = 0x0000,
	FU_USB_DEVICE_LANGID_ENGLISH_UNITED_STATES = 0x0409,
} FuUsbDeviceLangid;

FuUsbDevice *
fu_usb_device_get_parent(FuUsbDevice *self) G_GNUC_NON_NULL(1);

guint8
fu_usb_device_get_bus(FuUsbDevice *self);
guint8
fu_usb_device_get_address(FuUsbDevice *self);
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
fu_usb_device_get_configuration_index(FuUsbDevice *self, GError **error);
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
			       FuUsbDeviceDirection direction,
			       FuUsbDeviceRequestType request_type,
			       FuUsbDeviceRecipient recipient,
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
			      FuUsbDeviceClaimInterfaceFlags flags,
			      GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_usb_device_release_interface(FuUsbDevice *self,
				guint8 iface,
				FuUsbDeviceClaimInterfaceFlags flags,
				GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_usb_device_reset(FuUsbDevice *self, GError **error) G_GNUC_NON_NULL(1);
GPtrArray *
fu_usb_device_get_interfaces(FuUsbDevice *self, GError **error) G_GNUC_NON_NULL(1);
FuUsbInterface *
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
