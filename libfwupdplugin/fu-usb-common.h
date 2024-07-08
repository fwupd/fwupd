/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

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

#define FuUsbDirection	      GUsbDeviceDirection
#define FuUsbRecipient	      GUsbDeviceRecipient
#define FuUsbRequestType      GUsbDeviceRequestType
#define FuUsbEndpoint	      GUsbEndpoint
#define FuUsbInterface	      GUsbInterface
#define FuUsbDeviceClaimFlags GUsbDeviceClaimInterfaceFlags

#define FU_USB_DEVICE_CLAIM_FLAG_KERNEL_DRIVER G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER
#define FU_USB_DEVICE_CLAIM_FLAG_NONE	       G_USB_DEVICE_CLAIM_INTERFACE_NONE

#define FU_USB_CLASS_APPLICATION_SPECIFIC G_USB_DEVICE_CLASS_APPLICATION_SPECIFIC
#define FU_USB_CLASS_AUDIO		  G_USB_DEVICE_CLASS_AUDIO
#define FU_USB_CLASS_AUDIO_VIDEO	  G_USB_DEVICE_CLASS_AUDIO_VIDEO
#define FU_USB_CLASS_BILLBOARD		  G_USB_DEVICE_CLASS_BILLBOARD
#define FU_USB_CLASS_CDC_DATA		  G_USB_DEVICE_CLASS_CDC_DATA
#define FU_USB_CLASS_COMMUNICATIONS	  G_USB_DEVICE_CLASS_COMMUNICATIONS
#define FU_USB_CLASS_CONTENT_SECURITY	  G_USB_DEVICE_CLASS_CONTENT_SECURITY
#define FU_USB_CLASS_DIAGNOSTIC		  G_USB_DEVICE_CLASS_DIAGNOSTIC
#define FU_USB_CLASS_HID		  G_USB_DEVICE_CLASS_HID
#define FU_USB_CLASS_HUB		  G_USB_DEVICE_CLASS_HUB
#define FU_USB_CLASS_IMAGE		  G_USB_DEVICE_CLASS_IMAGE
#define FU_USB_CLASS_INTERFACE_DESC	  G_USB_DEVICE_CLASS_INTERFACE_DESC
#define FU_USB_CLASS_MASS_STORAGE	  G_USB_DEVICE_CLASS_MASS_STORAGE
#define FU_USB_CLASS_MISCELLANEOUS	  G_USB_DEVICE_CLASS_MISCELLANEOUS
#define FU_USB_CLASS_PERSONAL_HEALTHCARE  G_USB_DEVICE_CLASS_PERSONAL_HEALTHCARE
#define FU_USB_CLASS_PHYSICAL		  G_USB_DEVICE_CLASS_PHYSICAL
#define FU_USB_CLASS_PRINTER		  G_USB_DEVICE_CLASS_PRINTER
#define FU_USB_CLASS_SMART_CARD		  G_USB_DEVICE_CLASS_SMART_CARD
#define FU_USB_CLASS_VENDOR_SPECIFIC	  G_USB_DEVICE_CLASS_VENDOR_SPECIFIC
#define FU_USB_CLASS_VIDEO		  G_USB_DEVICE_CLASS_VIDEO
#define FU_USB_CLASS_WIRELESS_CONTROLLER  G_USB_DEVICE_CLASS_WIRELESS_CONTROLLER

#define FU_USB_DIRECTION_DEVICE_TO_HOST G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST
#define FU_USB_DIRECTION_HOST_TO_DEVICE G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE

#define FU_USB_LANGID_ENGLISH_UNITED_STATES G_USB_DEVICE_LANGID_ENGLISH_UNITED_STATES

#define FU_USB_RECIPIENT_DEVICE	   G_USB_DEVICE_RECIPIENT_DEVICE
#define FU_USB_RECIPIENT_INTERFACE G_USB_DEVICE_RECIPIENT_INTERFACE

#define FU_USB_REQUEST_TYPE_CLASS  G_USB_DEVICE_REQUEST_TYPE_CLASS
#define FU_USB_REQUEST_TYPE_VENDOR G_USB_DEVICE_REQUEST_TYPE_VENDOR

#define fu_usb_endpoint_get_address		g_usb_endpoint_get_address
#define fu_usb_endpoint_get_maximum_packet_size g_usb_endpoint_get_maximum_packet_size
#define fu_usb_endpoint_get_direction		g_usb_endpoint_get_direction

#define fu_usb_interface_get_alternate g_usb_interface_get_alternate
#define fu_usb_interface_get_class     g_usb_interface_get_class
#define fu_usb_interface_get_endpoints g_usb_interface_get_endpoints
#define fu_usb_interface_get_extra     g_usb_interface_get_extra
#define fu_usb_interface_get_index     g_usb_interface_get_index
#define fu_usb_interface_get_number    g_usb_interface_get_number
#define fu_usb_interface_get_protocol  g_usb_interface_get_protocol
#define fu_usb_interface_get_subclass  g_usb_interface_get_subclass
