/*
 * Copyright 2010 Richard Hughes <richard@hughsie.com>
 * Copyright 2011 Hans de Goede <hdegoede@redhat.com>
 * Copyright 2011 Debarshi Ray <debarshir@src.gnome.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuUsbDevice"

#include "config.h"

#include "fu-device-event-private.h"
#include "fu-device-private.h"
#include "fu-dump.h"
#include "fu-mem.h"
#include "fu-string.h"
#include "fu-usb-bos-descriptor-private.h"
#include "fu-usb-device-fw-ds20.h"
#include "fu-usb-device-ms-ds20.h"
#include "fu-usb-device-private.h"
#include "fu-usb-interface-private.h"

/**
 * FuUsbDevice:
 *
 * A USB device.
 *
 * See also: [class@FuDevice], [class@FuHidDevice]
 */

typedef struct {
	libusb_device *usb_device;
	libusb_device_handle *handle;
	struct libusb_device_descriptor desc;
	guint8 busnum;
	guint8 devnum;
	gboolean interfaces_valid;
	gboolean bos_descriptors_valid;
	gboolean hid_descriptors_valid;
	GPtrArray *interfaces;	    /* (element-type FuUsbInterface) */
	GPtrArray *bos_descriptors; /* (element-type FuUsbBosDescriptor) */
	GPtrArray *hid_descriptors; /* (element-type GBytes) */
	gint configuration;
	GPtrArray *device_interfaces; /* (nullable) (element-type FuUsbDeviceInterface) */
	guint claim_retry_count;
	guint open_retry_count;
	FuDeviceLocker *usb_device_locker;
} FuUsbDevicePrivate;

typedef struct {
	guint8 number;
	gboolean claimed;
} FuUsbDeviceInterface;

static void
fu_usb_device_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuUsbDevice,
		       fu_usb_device,
		       FU_TYPE_DEVICE,
		       0,
		       G_ADD_PRIVATE(FuUsbDevice)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fu_usb_device_codec_iface_init));

enum { PROP_0, PROP_LIBUSB_DEVICE, PROP_LAST };

#define GET_PRIVATE(o) (fu_usb_device_get_instance_private(o))

#define FU_DEVICE_CLAIM_INTERFACE_DELAY	    500 /* ms */
#define FU_USB_DEVICE_OPEN_DELAY	    50	/* ms */

static gboolean
fu_usb_device_libusb_error_to_gerror(gint rc, GError **error)
{
	gint error_code = FWUPD_ERROR_INTERNAL;
	/* Put the rc in libusb's error enum so that gcc warns us if we're
	   missing an error code */
	enum libusb_error result = rc;

	switch (result) {
	case LIBUSB_SUCCESS:
		return TRUE;
	case LIBUSB_ERROR_INVALID_PARAM:
	case LIBUSB_ERROR_NOT_FOUND:
	case LIBUSB_ERROR_NO_MEM:
	case LIBUSB_ERROR_OTHER:
	case LIBUSB_ERROR_INTERRUPTED:
		error_code = FWUPD_ERROR_INTERNAL;
		break;
	case LIBUSB_ERROR_IO:
	case LIBUSB_ERROR_OVERFLOW:
	case LIBUSB_ERROR_PIPE:
		error_code = FWUPD_ERROR_READ;
		break;
	case LIBUSB_ERROR_TIMEOUT:
		error_code = FWUPD_ERROR_TIMED_OUT;
		break;
	case LIBUSB_ERROR_NOT_SUPPORTED:
		error_code = FWUPD_ERROR_NOT_SUPPORTED;
		break;
	case LIBUSB_ERROR_ACCESS:
		error_code = FWUPD_ERROR_PERMISSION_DENIED;
		break;
	case LIBUSB_ERROR_NO_DEVICE:
		error_code = FWUPD_ERROR_NOT_FOUND;
		break;
	case LIBUSB_ERROR_BUSY:
		error_code = FWUPD_ERROR_BUSY;
		break;
	default:
		break;
	}

	g_set_error(error, FWUPD_ERROR, error_code, "USB error: %s [%i]", libusb_strerror(rc), rc);

	return FALSE;
}

static gboolean
fu_usb_device_libusb_status_to_gerror(gint status, GError **error)
{
	gboolean ret = FALSE;

	switch (status) {
	case LIBUSB_TRANSFER_COMPLETED:
		ret = TRUE;
		break;
	case LIBUSB_TRANSFER_ERROR:
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "transfer failed");
		break;
	case LIBUSB_TRANSFER_TIMED_OUT:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_TIMED_OUT,
				    "transfer timed out");
		break;
	case LIBUSB_TRANSFER_CANCELLED:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "transfer cancelled");
		break;
	case LIBUSB_TRANSFER_STALL:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "endpoint stalled or request not supported");
		break;
	case LIBUSB_TRANSFER_NO_DEVICE:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "device was disconnected");
		break;
	case LIBUSB_TRANSFER_OVERFLOW:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "device sent more data than requested");
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "unknown status [%i]",
			    status);
	}
	return ret;
}

/**
 * fu_usb_device_get_dev: (skip):
 **/
libusb_device *
fu_usb_device_get_dev(FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), NULL);
	return priv->usb_device;
}

static gboolean
fu_usb_device_not_open_error(FuUsbDevice *self, GError **error)
{
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INTERNAL,
		    "device %04x:%04x has not been opened",
		    fu_usb_device_get_vid(self),
		    fu_usb_device_get_pid(self));
	return FALSE;
}

static void
fu_usb_device_invalidate(FuDevice *device)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);

	priv->interfaces_valid = FALSE;
	priv->bos_descriptors_valid = FALSE;
	priv->hid_descriptors_valid = FALSE;
	g_ptr_array_set_size(priv->interfaces, 0);
	g_ptr_array_set_size(priv->bos_descriptors, 0);
	g_ptr_array_set_size(priv->hid_descriptors, 0);
}

static void
fu_usb_device_set_dev(FuUsbDevice *self, struct libusb_device *usb_device)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);

	/* allow replacement */
	g_clear_pointer(&priv->usb_device, libusb_unref_device);
	if (usb_device != NULL)
		priv->usb_device = libusb_ref_device(usb_device);
}

static void
fu_usb_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuUsbDevice *device = FU_USB_DEVICE(object);
	FuUsbDevicePrivate *priv = GET_PRIVATE(device);
	switch (prop_id) {
	case PROP_LIBUSB_DEVICE:
		g_value_set_pointer(value, priv->usb_device);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_usb_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuUsbDevice *self = FU_USB_DEVICE(object);
	switch (prop_id) {
	case PROP_LIBUSB_DEVICE:
		fu_usb_device_set_dev(self, g_value_get_pointer(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_usb_device_finalize(GObject *object)
{
	FuUsbDevice *device = FU_USB_DEVICE(object);
	FuUsbDevicePrivate *priv = GET_PRIVATE(device);

	if (priv->handle != NULL)
		libusb_close(priv->handle);
	if (priv->usb_device != NULL)
		libusb_unref_device(priv->usb_device);
	if (priv->device_interfaces != NULL)
		g_ptr_array_unref(priv->device_interfaces);
	g_ptr_array_unref(priv->interfaces);
	g_ptr_array_unref(priv->bos_descriptors);
	g_ptr_array_unref(priv->hid_descriptors);

	G_OBJECT_CLASS(fu_usb_device_parent_class)->finalize(object);
}

static void
fu_usb_device_init(FuUsbDevice *device)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(device);
	priv->configuration = -1;
	priv->interfaces = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	priv->bos_descriptors = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	priv->hid_descriptors = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);
	fu_device_set_acquiesce_delay(FU_DEVICE(device), 2500);
	fu_device_retry_add_recovery(FU_DEVICE(device), FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, NULL);
	fu_device_retry_add_recovery(FU_DEVICE(device),
				     FWUPD_ERROR,
				     FWUPD_ERROR_PERMISSION_DENIED,
				     NULL);
}

/**
 * fu_usb_device_set_claim_retry_count:
 * @self: a #FuUsbDevice
 * @claim_retry_count: integer
 *
 * Sets the number of tries we should attempt when claiming the device.
 * Applies to all interfaces associated with this device.
 *
 * Since: 1.9.10
 **/
void
fu_usb_device_set_claim_retry_count(FuUsbDevice *self, guint claim_retry_count)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_USB_DEVICE(self));
	priv->claim_retry_count = claim_retry_count;
}

/**
 * fu_usb_device_get_claim_retry_count:
 * @self: a #FuUsbDevice
 *
 * Gets the number of tries we should attempt when claiming the device.
 *
 * Returns: integer, or `0` if no attempt should be made.
 *
 * Since: 1.9.10
 **/
guint
fu_usb_device_get_claim_retry_count(FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), G_MAXUINT);
	return priv->claim_retry_count;
}

/**
 * fu_usb_device_set_open_retry_count:
 * @self: a #FuUsbDevice
 * @open_retry_count: integer
 *
 * Sets the number of tries we should attempt when opening the device.
 *
 * Since: 1.9.9
 **/
void
fu_usb_device_set_open_retry_count(FuUsbDevice *self, guint open_retry_count)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_USB_DEVICE(self));
	priv->open_retry_count = open_retry_count;
}

/**
 * fu_usb_device_get_open_retry_count:
 * @self: a #FuUsbDevice
 *
 * Sets the number of tries we should attempt when opening the device.
 *
 * Returns: integer, or `0` if no attempt should be made.
 *
 * Since: 1.9.9
 **/
guint
fu_usb_device_get_open_retry_count(FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), G_MAXUINT);
	return priv->open_retry_count;
}

/**
 * fu_usb_device_set_configuration:
 * @device: a #FuUsbDevice
 * @configuration: the configuration value to set
 *
 * Set the active bConfigurationValue for the device.
 *
 * Since: 1.7.4
 **/
void
fu_usb_device_set_configuration(FuUsbDevice *device, gint configuration)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(device);
	g_return_if_fail(FU_IS_USB_DEVICE(device));
	priv->configuration = configuration;
}

/**
 * fu_usb_device_add_interface:
 * @device: a #FuUsbDevice
 * @number: bInterfaceNumber of the interface
 *
 * Adds an interface that will be claimed on `->open()` and released on `->close()`.
 *
 * Since: 1.7.4
 **/
void
fu_usb_device_add_interface(FuUsbDevice *device, guint8 number)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(device);
	FuUsbDeviceInterface *iface;

	g_return_if_fail(FU_IS_USB_DEVICE(device));

	if (priv->device_interfaces == NULL)
		priv->device_interfaces = g_ptr_array_new_with_free_func(g_free);

	/* check for existing */
	for (guint i = 0; i < priv->device_interfaces->len; i++) {
		iface = g_ptr_array_index(priv->device_interfaces, i);
		if (iface->number == number)
			return;
	}

	/* add new */
	iface = g_new0(FuUsbDeviceInterface, 1);
	iface->number = number;
	g_ptr_array_add(priv->device_interfaces, iface);
}

static gboolean
fu_usb_device_query_hub(FuUsbDevice *self, GError **error)
{
	gsize sz = 0;
	guint16 value = 0x29;
	guint8 data[0x0c] = {0x0};
	g_autoptr(GString) hub = g_string_new(NULL);

	/* longer descriptor for SuperSpeed */
	if (fu_usb_device_get_spec(self) >= 0x0300)
		value = 0x2a;
	if (!fu_usb_device_control_transfer(self,
					    FU_USB_DIRECTION_DEVICE_TO_HOST,
					    FU_USB_REQUEST_TYPE_CLASS,
					    FU_USB_RECIPIENT_DEVICE,
					    0x06, /* LIBUSB_REQUEST_GET_DESCRIPTOR */
					    value << 8,
					    0x00,
					    data,
					    sizeof(data),
					    &sz,
					    1000,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to get USB descriptor: ");
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "HUB_DT", data, sz);

	/* for USB 3: size is fixed as max ports is 15,
	 * for USB 2: size is variable as max ports is 255 */
	if (fu_usb_device_get_spec(self) >= 0x0300 && sz == 0x0C) {
		g_string_append_printf(hub, "%02X", data[0x0B]);
		g_string_append_printf(hub, "%02X", data[0x0A]);
	} else if (sz >= 9) {
		guint8 numbytes = fu_common_align_up(data[2] + 1, 0x03) / 8;
		for (guint i = 0; i < numbytes; i++) {
			guint8 tmp = 0x0;
			if (!fu_memread_uint8_safe(data, sz, 7 + i, &tmp, error))
				return FALSE;
			g_string_append_printf(hub, "%02X", tmp);
		}
	}
	if (hub->len > 0)
		fu_device_add_instance_str(FU_DEVICE(self), "HUB", hub->str);
	return fu_device_build_instance_id_full(FU_DEVICE(self),
						FU_DEVICE_INSTANCE_FLAG_GENERIC |
						    FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						    FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						error,
						"USB",
						"VID",
						"PID",
						"HUB",
						NULL);
}

static gboolean
fu_usb_device_claim_interface_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	FuUsbDeviceInterface *iface = (FuUsbDeviceInterface *)user_data;
	return fu_usb_device_claim_interface(self,
					     iface->number,
					     FU_USB_DEVICE_CLAIM_FLAG_KERNEL_DRIVER,
					     error);
}

static gboolean
fu_usb_device_open_internal(FuUsbDevice *self, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	gint rc;

	/* sanity check */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;
	if (priv->handle != NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    "device %04x:%04x is already open",
			    fu_usb_device_get_vid(self),
			    fu_usb_device_get_pid(self));
		return FALSE;
	}

	/* open device */
	rc = libusb_open(priv->usb_device, &priv->handle);
	if (!fu_usb_device_libusb_error_to_gerror(rc, error)) {
		if (priv->handle != NULL)
			libusb_close(priv->handle);
		priv->handle = NULL;
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_usb_device_open_retry_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	return fu_usb_device_open_internal(self, error);
}

static gboolean
fu_usb_device_open_full(FuUsbDevice *self, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->open_retry_count > 0) {
		return fu_device_retry_full(FU_DEVICE(self),
					    fu_usb_device_open_retry_cb,
					    priv->open_retry_count,
					    FU_USB_DEVICE_OPEN_DELAY,
					    NULL,
					    error);
	}
	return fu_usb_device_open_internal(self, error);
}

static gboolean
fu_usb_device_close_internal(FuUsbDevice *self, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;
	if (priv->handle == NULL)
		return fu_usb_device_not_open_error(self, error);

	libusb_close(priv->handle);
	priv->handle = NULL;
	return TRUE;
}

static gboolean
fu_usb_device_set_configuration_internal(FuUsbDevice *self, gint configuration, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	gint rc;
	gint config_tmp = 0;

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;
	if (priv->handle == NULL)
		return fu_usb_device_not_open_error(self, error);

	/* verify we've not already set the same configuration */
	rc = libusb_get_configuration(priv->handle, &config_tmp);
	if (rc != LIBUSB_SUCCESS)
		return fu_usb_device_libusb_error_to_gerror(rc, error);
	if (config_tmp == configuration)
		return TRUE;

	/* different, so change */
	rc = libusb_set_configuration(priv->handle, configuration);
	return fu_usb_device_libusb_error_to_gerror(rc, error);
}

static gboolean
fu_usb_device_open(FuDevice *device, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* self tests */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* open */
	if (!fu_usb_device_open_full(self, error)) {
		g_prefix_error(error, "failed to open device: ");
		return FALSE;
	}

	/* if set */
	if (priv->configuration >= 0) {
		if (!fu_usb_device_set_configuration_internal(self, priv->configuration, error)) {
			g_prefix_error(error, "failed to set configuration: ");
			return FALSE;
		}
	}

	/* claim interfaces */
	for (guint i = 0; priv->device_interfaces != NULL && i < priv->device_interfaces->len;
	     i++) {
		FuUsbDeviceInterface *iface = g_ptr_array_index(priv->device_interfaces, i);
		if (priv->claim_retry_count > 0) {
			if (!fu_device_retry_full(device,
						  fu_usb_device_claim_interface_cb,
						  priv->claim_retry_count,
						  FU_DEVICE_CLAIM_INTERFACE_DELAY,
						  iface,
						  error)) {
				g_prefix_error(error,
					       "failed to claim interface 0x%02x with retries: ",
					       iface->number);
				return FALSE;
			}
		} else {
			if (!fu_usb_device_claim_interface(self,
							   iface->number,
							   FU_USB_DEVICE_CLAIM_FLAG_KERNEL_DRIVER,
							   error)) {
				g_prefix_error(error,
					       "failed to claim interface 0x%02x: ",
					       iface->number);
				return FALSE;
			}
		}
		iface->claimed = TRUE;
	}
	return TRUE;
}

/**
 * fu_usb_device_get_bus:
 * @self: a #FuUsbDevice
 *
 * Gets the USB bus number for the device.
 *
 * Return value: The 8-bit bus number
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_device_get_bus(FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return 0x0;
	return priv->busnum;
}

/**
 * fu_usb_device_get_address:
 * @self: a #FuUsbDevice
 *
 * Gets the USB address for the device.
 *
 * Return value: The 8-bit address
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_device_get_address(FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return 0x0;
	return priv->devnum;
}

static guint8
fu_usb_device_get_manufacturer_index(FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), 0);
	return priv->desc.iManufacturer;
}

static guint8
fu_usb_device_get_product_index(FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), 0);
	return priv->desc.iProduct;
}

/* not defined in FreeBSD */
#ifndef HAVE_LIBUSB_GET_PARENT
static libusb_device *
libusb_get_parent(libusb_device *dev)
{
	return NULL;
}
#endif

static void
fu_usb_device_build_parent_port_number(GString *str, libusb_device *dev)
{
	libusb_device *parent = libusb_get_parent(dev);
	if (parent != NULL)
		fu_usb_device_build_parent_port_number(str, parent);
	g_string_append_printf(str, "%02x:", libusb_get_port_number(dev));
}

static gchar *
fu_usb_device_build_physical_id(struct libusb_device *dev)
{
	GString *platform_id;

	/* build a topology of the device */
	platform_id = g_string_new("usb:");
	g_string_append_printf(platform_id, "%02x:", libusb_get_bus_number(dev));
	fu_usb_device_build_parent_port_number(platform_id, dev);
	g_string_truncate(platform_id, platform_id->len - 1);
	return g_string_free(platform_id, FALSE);
}

static gboolean
fu_usb_device_setup(FuDevice *device, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* get vendor */
	if (fu_device_get_vendor(device) == NULL) {
		guint idx = fu_usb_device_get_manufacturer_index(self);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			g_autoptr(GError) error_local = NULL;
			tmp = fu_usb_device_get_string_descriptor(self, idx, &error_local);
			if (tmp != NULL)
				fu_device_set_vendor(device, g_strchomp(tmp));
			else
				g_debug(
				    "failed to load manufacturer string for usb device %u:%u: %s",
				    fu_usb_device_get_bus(self),
				    fu_usb_device_get_address(self),
				    error_local->message);
		}
	}

	/* get product */
	if (fu_device_get_name(device) == NULL) {
		guint idx = fu_usb_device_get_product_index(self);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			g_autoptr(GError) error_local = NULL;
			tmp = fu_usb_device_get_string_descriptor(self, idx, &error_local);
			if (tmp != NULL)
				fu_device_set_name(device, g_strchomp(tmp));
			else
				g_debug("failed to load product string for usb device %u:%u: %s",
					fu_usb_device_get_bus(self),
					fu_usb_device_get_address(self),
					error_local->message);
		}
	}

	/* get serial number */
	if (!fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_NO_SERIAL_NUMBER) &&
	    fu_device_get_serial(device) == NULL) {
		guint idx = fu_usb_device_get_serial_number_index(self);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			g_autoptr(GError) error_local = NULL;
			tmp = fu_usb_device_get_string_descriptor(self, idx, &error_local);
			if (tmp != NULL)
				fu_device_set_serial(device, g_strchomp(tmp));
			else
				g_debug(
				    "failed to load serial number string for usb device %u:%u: %s",
				    fu_usb_device_get_bus(self),
				    fu_usb_device_get_address(self),
				    error_local->message);
		}
	}

	/* get the hub descriptor if this is a hub */
	if (fu_usb_device_get_class(self) == FU_USB_CLASS_HUB) {
		if (!fu_usb_device_query_hub(self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_usb_device_ready(FuDevice *device, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	g_autoptr(GPtrArray) intfs = NULL;

	/* self tests */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* get the interface GUIDs */
	intfs = fu_usb_device_get_interfaces(self, error);
	if (intfs == NULL) {
		g_prefix_error(error, "failed to get interfaces: ");
		return FALSE;
	}

	/* add fallback icon if there is nothing added already */
	if (fu_device_get_icons(device)->len == 0) {
		for (guint i = 0; i < intfs->len; i++) {
			FuUsbInterface *intf = g_ptr_array_index(intfs, i);

			/* Video: Video Control: i.e. a webcam */
			if (fu_usb_interface_get_class(intf) == FU_USB_CLASS_VIDEO &&
			    fu_usb_interface_get_subclass(intf) == 0x01) {
				fu_device_add_icon(device, "camera-web");
			}

			/* Audio */
			if (fu_usb_interface_get_class(intf) == FU_USB_CLASS_AUDIO)
				fu_device_add_icon(device, "audio-card");

			/* Mass Storage */
			if (fu_usb_interface_get_class(intf) == FU_USB_CLASS_MASS_STORAGE)
				fu_device_add_icon(device, "drive-harddisk");

			/* Printer */
			if (fu_usb_interface_get_class(intf) == FU_USB_CLASS_PRINTER)
				fu_device_add_icon(device, "printer");
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_usb_device_close(FuDevice *device, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* release interfaces, ignoring errors */
	for (guint i = 0; priv->device_interfaces != NULL && i < priv->device_interfaces->len;
	     i++) {
		FuUsbDeviceInterface *iface = g_ptr_array_index(priv->device_interfaces, i);
		FuUsbDeviceClaimFlags claim_flags = FU_USB_DEVICE_CLAIM_FLAG_NONE;
		g_autoptr(GError) error_local = NULL;
		if (!iface->claimed)
			continue;
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG)) {
			g_debug("re-binding kernel driver as not waiting for replug");
			claim_flags |= FU_USB_DEVICE_CLAIM_FLAG_KERNEL_DRIVER;
		}
		if (!fu_usb_device_release_interface(self,
						     iface->number,
						     claim_flags,
						     &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) ||
			    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INTERNAL)) {
				g_debug("failed to release interface 0x%02x: %s",
					iface->number,
					error_local->message);
			} else {
				g_warning("failed to release interface 0x%02x: %s",
					  iface->number,
					  error_local->message);
			}
		}
		iface->claimed = FALSE;
	}

	/* success */
	return fu_usb_device_close_internal(self, error);
}

static gboolean
fu_usb_device_probe_bos_descriptor(FuUsbDevice *self, FuUsbBosDescriptor *bos, GError **error)
{
	GBytes *extra = fu_usb_bos_descriptor_get_extra(bos);
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) ds20 = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* sanity check */
	if (g_bytes_get_size(extra) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "zero sized data");
		return FALSE;
	}

	/* parse either type */
	stream = g_memory_input_stream_new_from_bytes(extra);
	ds20 = fu_firmware_new_from_gtypes(stream,
					   0x0,
					   FWUPD_INSTALL_FLAG_NONE,
					   error,
					   FU_TYPE_USB_DEVICE_FW_DS20,
					   FU_TYPE_USB_DEVICE_MS_DS20,
					   G_TYPE_INVALID);
	if (ds20 == NULL) {
		g_prefix_error(error, "failed to parse: ");
		return FALSE;
	}
	str = fu_firmware_to_string(ds20);
	g_debug("DS20: %s", str);

	/* set the quirks onto the device */
	if (!fu_usb_device_ds20_apply_to_device(FU_USB_DEVICE_DS20(ds20), self, error)) {
		g_prefix_error(error, "failed to apply DS20 data: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GPtrArray *
fu_usb_device_get_bos_descriptors(FuUsbDevice *self, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* get all BOS descriptors */
	if (!priv->bos_descriptors_valid) {
		gint rc;
		guint8 num_device_caps;
		struct libusb_bos_descriptor *bos = NULL;

		/* sanity check */
		if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "not supported for emulated device");
			return NULL;
		}
		if (priv->handle == NULL) {
			fu_usb_device_not_open_error(self, error);
			return NULL;
		}
		if (fu_usb_device_get_spec(self) <= 0x0200) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not available as bcdUSB 0x%04x <= 0x0200",
				    fu_usb_device_get_spec(self));
			return NULL;
		}

		rc = libusb_get_bos_descriptor(priv->handle, &bos);
		if (!fu_usb_device_libusb_error_to_gerror(rc, error))
			return NULL;
#ifdef __FreeBSD__
		num_device_caps = bos->bNumDeviceCapabilities;
#else
		num_device_caps = bos->bNumDeviceCaps;
#endif
		for (guint i = 0; i < num_device_caps; i++) {
			FuUsbBosDescriptor *bos_descriptor = NULL;
			struct libusb_bos_dev_capability_descriptor *bos_cap =
			    bos->dev_capability[i];
			bos_descriptor = fu_usb_bos_descriptor_new(bos_cap);
			g_ptr_array_add(priv->bos_descriptors, bos_descriptor);
		}
		libusb_free_bos_descriptor(bos);
		priv->bos_descriptors_valid = TRUE;
	}

	/* success */
	return g_ptr_array_ref(priv->bos_descriptors);
}

static gboolean
fu_usb_device_probe_bos_descriptors(FuUsbDevice *self, GError **error)
{
	g_autoptr(FuDeviceLocker) usb_locker = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) bos_descriptors = NULL;

	/* already matched a quirk entry */
	if (fu_device_has_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_NO_PROBE)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not probing");
		return FALSE;
	}

	/* not supported, so there is no point opening */
	if (fu_usb_device_get_spec(self) <= 0x0200) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "not available as bcdUSB 0x%04x <= 0x0200",
			    fu_usb_device_get_spec(self));
		return FALSE;
	}

	usb_locker = fu_device_locker_new_full(self,
					       (FuDeviceLockerFunc)fu_usb_device_open_internal,
					       (FuDeviceLockerFunc)fu_usb_device_close_internal,
					       error);
	if (usb_locker == NULL) {
		fu_error_convert(error);
		return FALSE;
	}
	bos_descriptors = fu_usb_device_get_bos_descriptors(self, &error_local);
	if (bos_descriptors == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INTERNAL)) {
			g_debug("ignoring missing BOS descriptor: %s", error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		fu_error_convert(error);
		return FALSE;
	}
	for (guint i = 0; i < bos_descriptors->len; i++) {
		FuUsbBosDescriptor *bos = g_ptr_array_index(bos_descriptors, i);
		g_autoptr(GError) error_loop = NULL;

		if (fu_usb_bos_descriptor_get_capability(bos) != 0x5)
			continue;
		if (!fu_usb_device_probe_bos_descriptor(self, bos, &error_loop)) {
			g_warning("failed to parse platform BOS descriptor: %s",
				  error_loop->message);
		}
	}
	return TRUE;
}

static gboolean
fu_usb_device_probe(FuDevice *device, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	gint rc;
	guint16 release;
	g_autofree gchar *vendor_id = NULL;
	g_autoptr(GError) error_bos = NULL;
	g_autoptr(GPtrArray) intfs = NULL;

	/* open not required to get the descriptor */
	if (priv->usb_device != NULL) {
		rc = libusb_get_device_descriptor(priv->usb_device, &priv->desc);
		if (rc != LIBUSB_SUCCESS) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Failed to get USB descriptor for device: %s",
				    libusb_strerror(rc));
			return FALSE;
		}
		priv->busnum = libusb_get_bus_number(priv->usb_device);
		priv->devnum = libusb_get_device_address(priv->usb_device);
	}

	/* this does not change on plug->unplug->plug */
	if (priv->usb_device != NULL) {
		g_autofree gchar *platform_id = fu_usb_device_build_physical_id(priv->usb_device);
		fu_device_set_physical_id(device, platform_id);
	}

	/* set vendor ID */
	vendor_id = g_strdup_printf("USB:0x%04X", fu_usb_device_get_vid(self));
	fu_device_add_vendor_id(device, vendor_id);

	/* set the version if the release has been set */
	release = fu_usb_device_get_release(self);
	if (release != 0x0 &&
	    fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_BCD);
		fu_device_set_version_raw(device, release);
	}

	/* add GUIDs in order of priority */
	fu_device_add_instance_u16(device, "VID", fu_usb_device_get_vid(self));
	fu_device_add_instance_u16(device, "PID", fu_usb_device_get_pid(self));
	fu_device_add_instance_u16(device, "REV", release);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_GENERIC |
					     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "USB",
					 "VID",
					 NULL);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_GENERIC |
					     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
					     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "USB",
					 "VID",
					 "PID",
					 NULL);
	if (fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_ADD_INSTANCE_ID_REV)) {
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 "USB",
						 "VID",
						 "PID",
						 "REV",
						 NULL);
	}

	/* add the interface GUIDs */
	intfs = fu_usb_device_get_interfaces(self, error);
	if (intfs == NULL) {
		g_prefix_error(error, "failed to get interfaces: ");
		return FALSE;
	}
	for (guint i = 0; i < intfs->len; i++) {
		FuUsbInterface *intf = g_ptr_array_index(intfs, i);
		fu_device_add_instance_u8(device, "CLASS", fu_usb_interface_get_class(intf));
		fu_device_add_instance_u8(device, "SUBCLASS", fu_usb_interface_get_subclass(intf));
		fu_device_add_instance_u8(device, "PROT", fu_usb_interface_get_protocol(intf));
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 "USB",
						 "CLASS",
						 NULL);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 "USB",
						 "CLASS",
						 "SUBCLASS",
						 NULL);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 "USB",
						 "CLASS",
						 "SUBCLASS",
						 "PROT",
						 NULL);
	}

	/* add 2 levels of parent IDs */
	if (fu_device_get_physical_id(device) != NULL) {
		g_autofree gchar *platform_id_tmp = g_strdup(fu_device_get_physical_id(device));
		for (guint i = 0; i < 2; i++) {
			gchar *tok = g_strrstr(platform_id_tmp, ":");
			if (tok == NULL)
				break;
			*tok = '\0';
			if (g_strcmp0(platform_id_tmp, "usb") == 0)
				break;
			fu_device_add_parent_physical_id(device, platform_id_tmp);
		}
	}

	/* parse the platform capability BOS descriptors for quirks */
	if (!fu_usb_device_probe_bos_descriptors(self, &error_bos)) {
		if (g_error_matches(error_bos, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_debug("ignoring: %s", error_bos->message);
		} else {
			g_warning("failed to load BOS descriptor from USB device: %s",
				  error_bos->message);
		}
	}

	/* success */
	return TRUE;
}

/**
 * fu_usb_device_get_vid:
 * @self: a #FuUsbDevice
 *
 * Gets the device vendor code.
 *
 * Returns: integer, or 0x0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint16
fu_usb_device_get_vid(FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), 0x0000);
	return priv->desc.idVendor;
}

/**
 * fu_usb_device_get_pid:
 * @self: a #FuUsbDevice
 *
 * Gets the device product code.
 *
 * Returns: integer, or 0x0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint16
fu_usb_device_get_pid(FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), 0x0000);
	return priv->desc.idProduct;
}

/**
 * fu_usb_device_get_release:
 * @self: a #FuUsbDevice
 *
 * Gets the device release.
 *
 * Returns: integer, or 0x0 if unset or invalid
 *
 * Since: 2.0.0
 **/
guint16
fu_usb_device_get_release(FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), 0x0000);
	return priv->desc.bcdDevice;
}

/**
 * fu_usb_device_get_spec:
 * @self: a #FuUsbDevice
 *
 * Gets the string USB revision for the device.
 *
 * Returns: a specification revision in BCD format, or 0x0 if not supported
 *
 * Since: 1.3.4
 **/
guint16
fu_usb_device_get_spec(FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), 0x0);
	return priv->desc.bcdUSB;
}

/**
 * fu_usb_device_get_class:
 * @self: a #FuUsbDevice
 *
 * Gets the device class, typically a #FuUsbClass.
 *
 * Return value: a #FuUsbClass, e.g. %FU_USB_CLASS_HUB.
 *
 * Since: 2.0.0
 **/
FuUsbClass
fu_usb_device_get_class(FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), 0x0);
	return priv->desc.bDeviceClass;
}

/**
 * fu_usb_device_find_udev_device:
 * @device: a #FuUsbDevice
 * @error: (nullable): optional return location for an error
 *
 * Gets the matching #FuUdevDevice for the #FuUsbDevice.
 *
 * Returns: (transfer full): a #FuUdevDevice, or NULL if unset or invalid
 *
 * Since: 1.3.2
 **/
FuDevice *
fu_usb_device_find_udev_device(FuUsbDevice *self, GError **error)
{
#if defined(HAVE_GUDEV)
	g_autoptr(GList) devices = NULL;
	g_autoptr(GUdevClient) gudev_client = g_udev_client_new(NULL);

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find all tty devices */
	devices = g_udev_client_query_by_subsystem(gudev_client, "usb");
	for (GList *l = devices; l != NULL; l = l->next) {
		GUdevDevice *dev = G_UDEV_DEVICE(l->data);

		/* check correct device */
		if (g_udev_device_get_sysfs_attr_as_int(dev, "busnum") !=
		    fu_usb_device_get_bus(self))
			continue;
		if (g_udev_device_get_sysfs_attr_as_int(dev, "devnum") !=
		    fu_usb_device_get_address(self))
			continue;

		/* success */
		g_debug("USB device %u:%u is %s",
			fu_usb_device_get_bus(self),
			fu_usb_device_get_address(self),
			g_udev_device_get_sysfs_path(dev));
		return FU_DEVICE(fu_udev_device_new(fu_device_get_context(FU_DEVICE(self)), dev));
	}

	/* failure */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "could not find sysfs device for %u:%u",
		    fu_usb_device_get_bus(self),
		    fu_usb_device_get_address(self));
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <gudev.h> is unavailable");
#endif
	return NULL;
}

static void
fu_usb_device_add_interface_internal(FuUsbDevice *self, FuUsbInterface *iface)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	for (guint i = 0; i < priv->interfaces->len; i++) {
		FuUsbInterface *iface_tmp = g_ptr_array_index(priv->interfaces, i);
		if (fu_usb_interface_get_number(iface) == fu_usb_interface_get_number(iface_tmp) &&
		    fu_usb_interface_get_alternate(iface) ==
			fu_usb_interface_get_alternate(iface_tmp)) {
			g_warning("not adding duplicate interface");
			return;
		}
	}
	g_ptr_array_add(priv->interfaces, g_object_ref(iface));
}

static void
fu_usb_device_incorporate(FuDevice *device, FuDevice *device_donor)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	FuUsbDevice *donor = FU_USB_DEVICE(device_donor);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	FuUsbDevicePrivate *priv_donor = GET_PRIVATE(donor);

	fu_usb_device_set_dev(self, priv_donor->usb_device);

	/* all descriptor fields */
	if (priv->desc.idVendor == 0x0)
		memcpy(&priv->desc, &priv_donor->desc, sizeof(priv->desc));

	if (priv->interfaces->len == 0) {
		for (guint i = 0; i < priv_donor->interfaces->len; i++) {
			FuUsbInterface *iface = g_ptr_array_index(priv_donor->interfaces, i);
			g_ptr_array_add(priv->interfaces, g_object_ref(iface));
		}
	}
	priv->interfaces_valid = TRUE;
	if (priv->bos_descriptors->len == 0) {
		for (guint i = 0; i < priv_donor->bos_descriptors->len; i++) {
			FuUsbBosDescriptor *bos_descriptor =
			    g_ptr_array_index(priv_donor->bos_descriptors, i);
			g_ptr_array_add(priv->bos_descriptors, g_object_ref(bos_descriptor));
		}
	}
	priv->bos_descriptors_valid = TRUE;
	if (priv->hid_descriptors->len == 0) {
		for (guint i = 0; i < priv_donor->hid_descriptors->len; i++) {
			GBytes *hid_descriptor = g_ptr_array_index(priv_donor->hid_descriptors, i);
			g_ptr_array_add(priv->hid_descriptors, g_bytes_ref(hid_descriptor));
		}
	}
	priv->hid_descriptors_valid = TRUE;
}

static gchar *
fu_usb_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16(version_raw, fu_device_get_version_format(device));
}

static gboolean
fu_udev_device_bind_driver(FuDevice *device,
			   const gchar *subsystem,
			   const gchar *driver,
			   GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	g_autoptr(FuDevice) udev_device = NULL;

	/* use udev for this */
	udev_device = fu_usb_device_find_udev_device(self, error);
	if (udev_device == NULL)
		return FALSE;
	return fu_device_bind_driver(udev_device, subsystem, driver, error);
}

static gboolean
fu_udev_device_unbind_driver(FuDevice *device, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	g_autoptr(FuDevice) udev_device = NULL;

	/* use udev for this */
	udev_device = fu_usb_device_find_udev_device(self, error);
	if (udev_device == NULL)
		return FALSE;
	return fu_device_unbind_driver(udev_device, error);
}

/**
 * fu_usb_device_control_transfer:
 * @self: a #FuUsbDevice
 * @request_type: the request type field for the setup packet
 * @request: the request field for the setup packet
 * @value: the value field for the setup packet
 * @idx: the index field for the setup packet
 * @data: (array length=length): a suitably-sized data buffer for
 * either input or output
 * @length: the length field for the setup packet.
 * @actual_length: (out) (optional): the actual number of bytes sent, or %NULL
 * @timeout: timeout timeout (in milliseconds) that this function should wait
 * before giving up due to no response being received. For an unlimited
 * timeout, use 0.
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Perform a USB control transfer.
 *
 * Warning: this function is synchronous, and cannot be cancelled.
 *
 * Return value: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_usb_device_control_transfer(FuUsbDevice *self,
			       FuUsbDirection direction,
			       FuUsbRequestType request_type,
			       FuUsbRecipient recipient,
			       guint8 request,
			       guint16 value,
			       guint16 idx,
			       guint8 *data,
			       gsize length,
			       gsize *actual_length,
			       guint timeout,
			       GCancellable *cancellable,
			       GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	gint rc;
	guint8 request_type_raw = 0;
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* build event key either for load or save */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		g_autofree gchar *data_base64 = g_base64_encode(data, length);
		event_id = g_strdup_printf("ControlTransfer:"
					   "Direction=0x%02x,"
					   "RequestType=0x%02x,"
					   "Recipient=0x%02x,"
					   "Request=0x%02x,"
					   "Value=0x%04x,"
					   "Idx=0x%04x,"
					   "Data=%s,"
					   "Length=0x%x",
					   direction,
					   request_type,
					   recipient,
					   request,
					   value,
					   idx,
					   data_base64,
					   (guint)length);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		gint64 rc_tmp;

		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return FALSE;
		rc_tmp = fu_device_event_get_i64(event, "Error", NULL);
		if (rc_tmp != G_MAXINT64)
			return fu_usb_device_libusb_error_to_gerror(rc_tmp, error);
		rc_tmp = fu_device_event_get_i64(event, "Status", NULL);
		if (rc_tmp != G_MAXINT64)
			return fu_usb_device_libusb_status_to_gerror(rc_tmp, error);
		return fu_device_event_copy_data(event, "Data", data, length, actual_length, error);
	}

	/* sanity check */
	if (priv->handle == NULL)
		return fu_usb_device_not_open_error(self, error);

	/* save */
	if (fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event = fu_device_save_event(FU_DEVICE(self), event_id);
	}

	/* munge back to flags */
	if (direction == FU_USB_DIRECTION_DEVICE_TO_HOST)
		request_type_raw |= 0x80;
	request_type_raw |= (request_type << 5);
	request_type_raw |= recipient;

	/* sync request */
	rc = libusb_control_transfer(priv->handle,
				     request_type_raw,
				     request,
				     value,
				     idx,
				     data,
				     length,
				     timeout);
	if (rc < 0) {
		if (!fu_usb_device_libusb_error_to_gerror(rc, error)) {
			if (event != NULL)
				fu_device_event_set_i64(event, "Error", rc);
			return FALSE;
		}
	}
	if (actual_length != NULL)
		*actual_length = rc;

	/* save */
	if (event != NULL)
		fu_device_event_set_data(event, "Data", data, rc);

	/* success */
	return TRUE;
}

/**
 * fu_usb_device_bulk_transfer:
 * @self: a #FuUsbDevice
 * @endpoint: the address of a valid endpoint to communicate with
 * @data: (array length=length): a suitably-sized data buffer for
 * either input or output
 * @length: the length field for the setup packet.
 * @actual_length: (out) (optional): the actual number of bytes sent, or %NULL
 * @timeout: timeout timeout (in milliseconds) that this function should wait
 * before giving up due to no response being received. For an unlimited
 * timeout, use 0.
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Perform a USB bulk transfer.
 *
 * Warning: this function is synchronous, and cannot be cancelled.
 *
 * Return value: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_usb_device_bulk_transfer(FuUsbDevice *self,
			    guint8 endpoint,
			    guint8 *data,
			    gsize length,
			    gsize *actual_length,
			    guint timeout,
			    GCancellable *cancellable,
			    GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	gint rc;
	gint transferred = 0;
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* build event key either for load or save */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		g_autofree gchar *data_base64 = g_base64_encode(data, length);
		event_id = g_strdup_printf("BulkTransfer:"
					   "Endpoint=0x%02x,"
					   "Data=%s,"
					   "Length=0x%x",
					   endpoint,
					   data_base64,
					   (guint)length);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		gint64 rc_tmp;

		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return FALSE;
		rc_tmp = fu_device_event_get_i64(event, "Error", NULL);
		if (rc_tmp != G_MAXINT64)
			return fu_usb_device_libusb_error_to_gerror(rc_tmp, error);
		rc_tmp = fu_device_event_get_i64(event, "Status", NULL);
		if (rc_tmp != G_MAXINT64)
			return fu_usb_device_libusb_status_to_gerror(rc_tmp, error);
		return fu_device_event_copy_data(event, "Data", data, length, actual_length, error);
	}

	/* sanity check */
	if (priv->handle == NULL)
		return fu_usb_device_not_open_error(self, error);

	/* save */
	if (fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event = fu_device_save_event(FU_DEVICE(self), event_id);
	}

	/* sync request */
	rc = libusb_bulk_transfer(priv->handle, endpoint, data, length, &transferred, timeout);
	if (!fu_usb_device_libusb_error_to_gerror(rc, error)) {
		if (event != NULL)
			fu_device_event_set_i64(event, "Error", rc);
		return FALSE;
	}
	if (actual_length != NULL)
		*actual_length = transferred;

	/* save */
	if (event != NULL)
		fu_device_event_set_data(event, "Data", data, transferred);

	/* success */
	return TRUE;
}

/**
 * fu_usb_device_interrupt_transfer:
 * @self: a #FuUsbDevice
 * @endpoint: the address of a valid endpoint to communicate with
 * @data: (array length=length): a suitably-sized data buffer for either input or output
 * @length: the length field for the setup packet.
 * @actual_length: (out) (optional): the actual number of bytes sent, or %NULL
 * @timeout: timeout (in milliseconds) that this function should wait -- use 0 for unlimited
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Perform a USB interrupt transfer.
 *
 * Warning: this function is synchronous, and cannot be cancelled.
 *
 * Return value: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_usb_device_interrupt_transfer(FuUsbDevice *self,
				 guint8 endpoint,
				 guint8 *data,
				 gsize length,
				 gsize *actual_length,
				 guint timeout,
				 GCancellable *cancellable,
				 GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	gint rc;
	gint transferred = 0;
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* build event key either for load or save */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		g_autofree gchar *data_base64 = g_base64_encode(data, length);
		event_id = g_strdup_printf("InterruptTransfer:"
					   "Endpoint=0x%02x,"
					   "Data=%s,"
					   "Length=0x%x",
					   endpoint,
					   data_base64,
					   (guint)length);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		gint64 rc_tmp;

		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return FALSE;
		rc_tmp = fu_device_event_get_i64(event, "Error", NULL);
		if (rc_tmp != G_MAXINT64)
			return fu_usb_device_libusb_error_to_gerror(rc_tmp, error);
		rc_tmp = fu_device_event_get_i64(event, "Status", NULL);
		if (rc_tmp != G_MAXINT64)
			return fu_usb_device_libusb_status_to_gerror(rc_tmp, error);
		return fu_device_event_copy_data(event, "Data", data, length, actual_length, error);
	}

	/* sanity check */
	if (priv->handle == NULL)
		return fu_usb_device_not_open_error(self, error);

	/* save */
	if (fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event = fu_device_save_event(FU_DEVICE(self), event_id);
	}

	/* sync request */
	rc = libusb_interrupt_transfer(priv->handle, endpoint, data, length, &transferred, timeout);
	if (!fu_usb_device_libusb_error_to_gerror(rc, error)) {
		if (event != NULL)
			fu_device_event_set_i64(event, "Error", rc);
		return FALSE;
	}
	if (actual_length != NULL)
		*actual_length = transferred;

	/* save */
	if (event != NULL)
		fu_device_event_set_data(event, "Data", data, transferred);

	/* success */
	return TRUE;
}

/**
 * fu_usb_device_reset:
 * @self: a #FuUsbDevice
 * @error: a #GError, or %NULL
 *
 * Perform a USB port reset to reinitialize a device.
 *
 * If the reset succeeds, the device will appear to disconnected and reconnected.
 * This means the @self will no longer be valid and should be closed and
 * rediscovered.
 *
 * This is a blocking function which usually incurs a noticeable delay.
 *
 * Return value: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_usb_device_reset(FuUsbDevice *self, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	gint rc;

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulating? */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;
	if (priv->handle == NULL)
		return fu_usb_device_not_open_error(self, error);

	rc = libusb_reset_device(priv->handle);
	if (rc == LIBUSB_ERROR_NOT_FOUND)
		return TRUE;
	return fu_usb_device_libusb_error_to_gerror(rc, error);
}

static gboolean
fu_usb_device_ensure_interfaces(FuUsbDevice *self, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	gint rc;
	struct libusb_config_descriptor *config;

	/* sanity check */
	if (priv->interfaces_valid)
		return TRUE;
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	rc = libusb_get_active_config_descriptor(priv->usb_device, &config);
	if (!fu_usb_device_libusb_error_to_gerror(rc, error))
		return FALSE;

	for (guint i = 0; i < config->bNumInterfaces; i++) {
		for (guint j = 0; j < (guint)config->interface[i].num_altsetting; j++) {
			const struct libusb_interface_descriptor *ifp =
			    &config->interface[i].altsetting[j];
			g_autoptr(FuUsbInterface) iface = fu_usb_interface_new(ifp);
			fu_usb_device_add_interface_internal(self, iface);
		}
	}
	libusb_free_config_descriptor(config);
	priv->interfaces_valid = TRUE;
	return TRUE;
}

/**
 * fu_usb_device_get_interfaces:
 * @self: a #FuUsbDevice
 * @error: a #GError, or %NULL
 *
 * Gets all the interfaces exported by the device.
 *
 * Return value: (transfer container) (element-type FuUsbInterface): an array of interfaces or %NULL
 *
 * Since: 2.0.0
 **/
GPtrArray *
fu_usb_device_get_interfaces(FuUsbDevice *self, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_usb_device_ensure_interfaces(self, error))
		return NULL;
	return g_ptr_array_ref(priv->interfaces);
}

/**
 * fu_usb_device_get_interface:
 * @self: a #FuUsbDevice
 * @class_id: a device class, e.g. 0xff for VENDOR
 * @subclass_id: a device subclass
 * @protocol_id: a protocol number
 * @error: a #GError, or %NULL
 *
 * Gets the first interface that matches the vendor class interface descriptor.
 * If you want to find all the interfaces that match (there may be other
 * 'alternate' interfaces you have to use fu_usb_device_get_interfaces() and
 * check each one manally.
 *
 * Return value: (transfer full): a #FuUsbInterface or %NULL for not found
 *
 * Since: 0.2.8
 **/
FuUsbInterface *
fu_usb_device_get_interface(FuUsbDevice *self,
			    guint8 class_id,
			    guint8 subclass_id,
			    guint8 protocol_id,
			    GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find the right data */
	if (!fu_usb_device_ensure_interfaces(self, error))
		return NULL;
	for (guint i = 0; i < priv->interfaces->len; i++) {
		FuUsbInterface *iface = g_ptr_array_index(priv->interfaces, i);
		if (fu_usb_interface_get_class(iface) != class_id)
			continue;
		if (fu_usb_interface_get_subclass(iface) != subclass_id)
			continue;
		if (fu_usb_interface_get_protocol(iface) != protocol_id)
			continue;
		return g_object_ref(iface);
	}

	/* nothing matched */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "no interface for class 0x%02x, "
		    "subclass 0x%02x and protocol 0x%02x",
		    class_id,
		    subclass_id,
		    protocol_id);
	return NULL;
}

/**
 * fu_usb_device_get_string_descriptor:
 * @desc_index: the index for the string descriptor to retrieve
 * @error: a #GError, or %NULL
 *
 * Get a string descriptor from the device. The returned string should be freed
 * with g_free() when no longer needed.
 *
 * Return value: a newly-allocated string holding the descriptor, or NULL on error.
 *
 * Since: 2.0.0
 **/
gchar *
fu_usb_device_get_string_descriptor(FuUsbDevice *self, guint8 desc_index, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	FuDeviceEvent *event;
	gint rc;
	unsigned char buf[128] = {0};
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* build event key either for load or save */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("GetStringDescriptor:DescIndex=0x%02x", desc_index);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		gint64 rc_tmp;
		g_autoptr(GBytes) bytes = NULL;

		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		rc_tmp = fu_device_event_get_i64(event, "Error", NULL);
		if (rc_tmp != G_MAXINT64) {
			fu_usb_device_libusb_error_to_gerror(rc_tmp, error);
			return NULL;
		}
		rc_tmp = fu_device_event_get_i64(event, "Status", NULL);
		if (rc_tmp != G_MAXINT64) {
			fu_usb_device_libusb_status_to_gerror(rc_tmp, error);
			return NULL;
		}
		bytes = fu_device_event_get_bytes(event, "Data", error);
		if (bytes == NULL)
			return NULL;
		return g_strndup(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
	}
	if (priv->handle == NULL) {
		fu_usb_device_not_open_error(self, error);
		return NULL;
	}

	rc = libusb_get_string_descriptor_ascii(priv->handle, desc_index, buf, sizeof(buf));
	if (rc < 0) {
		fu_usb_device_libusb_error_to_gerror(rc, error);
		return NULL;
	}

	/* save */
	if (fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event = fu_device_save_event(FU_DEVICE(self), event_id);
		fu_device_event_set_data(event, "Data", buf, sizeof(buf));
	}

	return g_strndup((const gchar *)buf, sizeof(buf));
}

/**
 * fu_usb_device_get_string_descriptor_bytes:
 * @desc_index: the index for the string descriptor to retrieve
 * @langid: the language ID
 * @error: a #GError, or %NULL
 *
 * Get a raw string descriptor from the device. The returned string should be freed
 * with g_bytes_unref() when no longer needed.
 * The descriptor will be at most 128 btes in length, if you need to
 * issue a request with either a smaller or larger descriptor, you can
 * use fu_usb_device_get_string_descriptor_bytes_full instead.
 *
 * Return value: (transfer full): a possibly UTF-16 string, or NULL on error.
 *
 * Since: 2.0.0
 **/
GBytes *
fu_usb_device_get_string_descriptor_bytes(FuUsbDevice *self,
					  guint8 desc_index,
					  guint16 langid,
					  GError **error)
{
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fu_usb_device_get_string_descriptor_bytes_full(self, desc_index, langid, 128, error);
}

/**
 * fu_usb_device_get_string_descriptor_bytes_full:
 * @desc_index: the index for the string descriptor to retrieve
 * @langid: the language ID
 * @length: size of the request data buffer
 * @error: a #GError, or %NULL
 *
 * Get a raw string descriptor from the device. The returned string should be freed
 * with g_bytes_unref() when no longer needed.
 *
 * Return value: (transfer full): a possibly UTF-16 string, or NULL on error.
 *
 * Since: 2.0.0
 **/
GBytes *
fu_usb_device_get_string_descriptor_bytes_full(FuUsbDevice *self,
					       guint8 desc_index,
					       guint16 langid,
					       gsize length,
					       GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	FuDeviceEvent *event;
	gint rc;
	g_autofree gchar *event_id = NULL;
	g_autofree guint8 *buf = g_malloc0(length);
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* emulating? */
	/* build event key either for load or save */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf(
		    "GetStringDescriptorBytes:DescIndex=0x%02x,Langid=0x%04x,Length=0x%x",
		    desc_index,
		    langid,
		    (guint)length);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		gint64 rc_tmp;
		g_autoptr(GBytes) bytes = NULL;

		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		rc_tmp = fu_device_event_get_i64(event, "Error", NULL);
		if (rc_tmp != G_MAXINT64) {
			fu_usb_device_libusb_error_to_gerror(rc_tmp, error);
			return NULL;
		}
		rc_tmp = fu_device_event_get_i64(event, "Status", NULL);
		if (rc_tmp != G_MAXINT64) {
			fu_usb_device_libusb_status_to_gerror(rc_tmp, error);
			return NULL;
		}
		bytes = fu_device_event_get_bytes(event, "Data", error);
		if (bytes == NULL)
			return NULL;
		return g_steal_pointer(&bytes);
	}

	if (priv->handle == NULL) {
		fu_usb_device_not_open_error(self, error);
		return NULL;
	}
	rc = libusb_get_string_descriptor(priv->handle, desc_index, langid, buf, length);
	if (rc < 0) {
		fu_usb_device_libusb_error_to_gerror(rc, error);
		return NULL;
	}

	/* save */
	if (fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event = fu_device_save_event(FU_DEVICE(self), event_id);
		fu_device_event_set_data(event, "Data", buf, rc);
	}

	/* success */
	return g_bytes_new(buf, rc);
}

/**
 * fu_usb_device_claim_interface:
 * @self: a #FuUsbDevice
 * @iface: bInterfaceNumber of the interface you wish to claim
 * @flags: #FuUsbDeviceClaimFlags
 * @error: a #GError, or %NULL
 *
 * Claim an interface of the device.
 *
 * Return value: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_usb_device_claim_interface(FuUsbDevice *self,
			      guint8 iface,
			      FuUsbDeviceClaimFlags flags,
			      GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	gint rc;

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulating? */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	if (priv->handle == NULL)
		return fu_usb_device_not_open_error(self, error);

	if (flags & FU_USB_DEVICE_CLAIM_FLAG_KERNEL_DRIVER) {
		rc = libusb_detach_kernel_driver(priv->handle, iface);
		if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_NOT_FOUND && /* No driver attached */
		    rc != LIBUSB_ERROR_NOT_SUPPORTED &&			    /* win32 */
		    rc != LIBUSB_ERROR_BUSY /* driver rebound already */)
			return fu_usb_device_libusb_error_to_gerror(rc, error);
	}

	rc = libusb_claim_interface(priv->handle, iface);
	return fu_usb_device_libusb_error_to_gerror(rc, error);
}

/**
 * fu_usb_device_release_interface:
 * @self: a #FuUsbDevice
 * @iface: bInterfaceNumber of the interface you wish to release
 * @flags: #FuUsbDeviceClaimFlags
 * @error: a #GError, or %NULL
 *
 * Release an interface of the device.
 *
 * Return value: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_usb_device_release_interface(FuUsbDevice *self,
				guint8 iface,
				FuUsbDeviceClaimFlags flags,
				GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	gint rc;

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulating? */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	if (priv->handle == NULL)
		return fu_usb_device_not_open_error(self, error);

	rc = libusb_release_interface(priv->handle, iface);
	if (rc != LIBUSB_SUCCESS)
		return fu_usb_device_libusb_error_to_gerror(rc, error);

	if (flags & FU_USB_DEVICE_CLAIM_FLAG_KERNEL_DRIVER) {
		rc = libusb_attach_kernel_driver(priv->handle, iface);
		if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_NOT_FOUND && /* No driver attached */
		    rc != LIBUSB_ERROR_NOT_SUPPORTED &&			    /* win32 */
		    rc != LIBUSB_ERROR_BUSY /* driver rebound already */)
			return fu_usb_device_libusb_error_to_gerror(rc, error);
	}

	return TRUE;
}

/**
 * fu_usb_device_get_configuration_index
 * @self: a #FuUsbDevice
 * @error: a #GError, or %NULL
 *
 * Get the index for the active Configuration string descriptor
 * ie, iConfiguration.
 *
 * Return value: a string descriptor index, or 0x0 on error
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_device_get_configuration_index(FuUsbDevice *self, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	FuDeviceEvent *event = NULL;
	gint rc;
	guint8 index;
	struct libusb_config_descriptor *config;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), 0x0);

	/* build event key either for load or save */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("GetConfigurationIndex");
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		g_autoptr(GBytes) bytes = NULL;
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return 0x0;
		bytes = fu_device_event_get_bytes(event, "Data", error);
		if (bytes == NULL)
			return 0x0;
		if (g_bytes_get_size(bytes) != 1) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no correct event data for %s",
				    event_id);
			return 0x0;
		}
		return ((const guint8 *)g_bytes_get_data(bytes, NULL))[0];
	}

	rc = libusb_get_active_config_descriptor(priv->usb_device, &config);
	if (rc != LIBUSB_SUCCESS)
		return fu_usb_device_libusb_error_to_gerror(rc, error);
	index = config->iConfiguration;

	/* save */
	if (fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event = fu_device_save_event(FU_DEVICE(self), event_id);
		fu_device_event_set_data(event, "Data", &index, sizeof(index));
	}

	libusb_free_config_descriptor(config);
	return index;
}

/**
 * fu_usb_device_get_serial_number_index:
 * @self: a #FuUsbDevice
 *
 * Gets the index for the Serial Number string descriptor.
 *
 * Return value: a string descriptor index.
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_device_get_serial_number_index(FuUsbDevice *self)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), 0x0);
	return priv->desc.iSerialNumber;
}

/**
 * fu_usb_device_get_custom_index:
 * @self: a #FuUsbDevice
 * @class_id: a device class, e.g. 0xff for VENDOR
 * @subclass_id: a device subclass
 * @protocol_id: a protocol number
 * @error: a #GError, or %NULL
 *
 * Gets the string index from the vendor class interface descriptor.
 *
 * Return value: a non-zero index, or 0x00 for failure
 *
 * Since: 2.0.0
 **/
guint8
fu_usb_device_get_custom_index(FuUsbDevice *self,
			       guint8 class_id,
			       guint8 subclass_id,
			       guint8 protocol_id,
			       GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	FuDeviceEvent *event;
	gint rc;
	guint8 idx = 0x00;
	struct libusb_config_descriptor *config;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), 0x0);
	g_return_val_if_fail(error == NULL || *error == NULL, 0x0);

	/* build event key either for load or save */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf(
		    "GetCustomIndex:ClassId=0x%02x,SubclassId=0x%02x,ProtocolId=0x%02x",
		    class_id,
		    subclass_id,
		    protocol_id);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		gint64 rc_tmp;
		g_autoptr(GBytes) bytes = NULL;

		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return 0x00;
		rc_tmp = fu_device_event_get_i64(event, "Error", NULL);
		if (rc_tmp != G_MAXINT64)
			return fu_usb_device_libusb_error_to_gerror(rc_tmp, error);
		rc_tmp = fu_device_event_get_i64(event, "Status", NULL);
		if (rc_tmp != G_MAXINT64)
			return fu_usb_device_libusb_status_to_gerror(rc_tmp, error);
		bytes = fu_device_event_get_bytes(event, "Data", error);
		if (bytes == NULL)
			return 0x00;
		if (g_bytes_get_size(bytes) != 1) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no correct event data for %s",
				    event_id);
			return 0x00;
		}
		return ((const guint8 *)g_bytes_get_data(bytes, NULL))[0];
	}

	rc = libusb_get_active_config_descriptor(priv->usb_device, &config);
	if (!fu_usb_device_libusb_error_to_gerror(rc, error))
		return 0x00;

	/* find the right data */
	for (guint i = 0; i < config->bNumInterfaces; i++) {
		const struct libusb_interface_descriptor *ifp = &config->interface[i].altsetting[0];
		if (ifp->bInterfaceClass != class_id)
			continue;
		if (ifp->bInterfaceSubClass != subclass_id)
			continue;
		if (ifp->bInterfaceProtocol != protocol_id)
			continue;
		idx = ifp->iInterface;
		break;
	}
	libusb_free_config_descriptor(config);

	/* nothing matched */
	if (idx == 0x00) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no vendor descriptor for class 0x%02x, "
			    "subclass 0x%02x and protocol 0x%02x",
			    class_id,
			    subclass_id,
			    protocol_id);
		return 0x0;
	}

	/* save */
	if (fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event = fu_device_save_event(FU_DEVICE(self), event_id);
		fu_device_event_set_data(event, "Data", &idx, sizeof(idx));
	}

	/* success */
	return idx;
}

/**
 * fu_usb_device_set_interface_alt:
 * @self: a #FuUsbDevice
 * @iface: bInterfaceNumber of the interface you wish to release
 * @alt: alternative setting number
 * @error: a #GError, or %NULL
 *
 * Sets an alternate setting on an interface.
 *
 * Return value: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_usb_device_set_interface_alt(FuUsbDevice *self, guint8 iface, guint8 alt, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	guint8 rc;

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulating? */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	if (priv->handle == NULL)
		return fu_usb_device_not_open_error(self, error);
	rc = libusb_set_interface_alt_setting(priv->handle, (gint)iface, (gint)alt);
	return fu_usb_device_libusb_error_to_gerror(rc, error);
}

static GBytes *
fu_usb_device_get_hid_descriptor_for_interface(FuUsbDevice *self,
					       FuUsbInterface *intf,
					       GError **error)
{
	GBytes *extra;
	gsize bufsz = 0;
	const guint8 *buf;
	gsize actual_length = 0;
	gsize buf2sz;
	guint16 buf2szle = 0;
	g_autofree guint8 *buf2 = NULL;

	extra = fu_usb_interface_get_extra(intf);
	if (extra == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "no data found on HID interface 0x%x",
			    fu_usb_interface_get_number(intf));
		return NULL;
	}
	buf = g_bytes_get_data(extra, &bufsz);
	if (bufsz < 9) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "invalid data on HID interface 0x%x",
			    fu_usb_interface_get_number(intf));
		return NULL;
	}
	if (buf[1] != LIBUSB_DT_HID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "invalid data on HID interface 0x%x, got 0x%x and expected 0x%x",
			    fu_usb_interface_get_number(intf),
			    buf[1],
			    (guint)LIBUSB_DT_HID);
		return NULL;
	}
	memcpy(&buf2szle, buf + 7, sizeof(buf2szle));
	buf2sz = GUINT16_FROM_LE(buf2szle);
	if (buf2sz == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "missing data on HID interface 0x%x",
			    fu_usb_interface_get_number(intf));
		return NULL;
	}
	g_debug("get 0x%x bytes of HID descriptor on iface 0x%x",
		(guint)buf2sz,
		fu_usb_interface_get_number(intf));

	/* get HID descriptor */
	buf2 = g_malloc0(buf2sz);
	if (!fu_usb_device_control_transfer(self,
					    FU_USB_DIRECTION_DEVICE_TO_HOST,
					    FU_USB_REQUEST_TYPE_STANDARD,
					    FU_USB_RECIPIENT_INTERFACE,
					    LIBUSB_REQUEST_GET_DESCRIPTOR,
					    LIBUSB_DT_REPORT << 8,
					    fu_usb_interface_get_number(intf),
					    buf2,
					    buf2sz,
					    &actual_length,
					    5000,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to get HID report descriptor: ");
		return NULL;
	}
	if (actual_length < buf2sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "invalid data on HID interface 0x%x, got 0x%x and expected 0x%x",
			    fu_usb_interface_get_number(intf),
			    (guint)actual_length,
			    (guint)buf2sz);
		return NULL;
	}

	/* success */
	return g_bytes_new_take(g_steal_pointer(&buf2), actual_length);
}

static gboolean
fu_usb_device_ensure_hid_descriptors(FuUsbDevice *self, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);

	/* sanity check */
	if (priv->hid_descriptors_valid)
		return TRUE;

	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not supported for emulated device");
		return FALSE;
	}
	if (priv->handle == NULL) {
		fu_usb_device_not_open_error(self, error);
		return FALSE;
	}

	if (!fu_usb_device_ensure_interfaces(self, error))
		return FALSE;
	for (guint i = 0; i < priv->interfaces->len; i++) {
		FuUsbInterface *intf = g_ptr_array_index(priv->interfaces, i);
		g_autoptr(GBytes) blob = NULL;
		if (fu_usb_interface_get_class(intf) != FU_USB_CLASS_HID)
			continue;
		blob = fu_usb_device_get_hid_descriptor_for_interface(self, intf, error);
		if (blob == NULL)
			return FALSE;
		g_ptr_array_add(priv->hid_descriptors, g_steal_pointer(&blob));
	}
	priv->hid_descriptors_valid = TRUE;
	return TRUE;
}

/**
 * fu_usb_device_get_hid_descriptors:
 * @self: a #FuUsbDevice
 * @error: a #GError, or %NULL
 *
 * Gets all the HID descriptors exported by the device.
 *
 * The first time this method is used the hardware is queried and then after that cached results
 * are returned. To invalidate the caches use fu_device_invalidate().
 *
 * Return value: (transfer container) (element-type GBytes): an array of HID descriptors
 *
 * Since: 2.0.0
 **/
GPtrArray *
fu_usb_device_get_hid_descriptors(FuUsbDevice *self, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_usb_device_ensure_hid_descriptors(self, error))
		return NULL;
	return g_ptr_array_ref(priv->hid_descriptors);
}

static gboolean
fu_usb_device_from_json(FwupdCodec *codec, JsonNode *json_node, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE(codec);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	JsonObject *json_object;

	/* sanity check */
	if (!JSON_NODE_HOLDS_OBJECT(json_node)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "not JSON object");
		return FALSE;
	}
	json_object = json_node_get_object(json_node);

	/* optional properties */
	tmp = json_object_get_string_member_with_default(json_object, "PlatformId", NULL);
	if (tmp != NULL)
		fu_device_set_physical_id(FU_DEVICE(self), tmp);
#if GLIB_CHECK_VERSION(2, 80, 0)
	tmp = json_object_get_string_member_with_default(json_object, "Created", NULL);
	if (tmp != NULL) {
		g_autoptr(GDateTime) created_new = g_date_time_new_from_iso8601(tmp, NULL);
		if (created_new == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "cannot parse ISO8601 date: %s",
				    tmp);
			return FALSE;
		}
		fu_device_set_created_usec(FU_DEVICE(self), g_date_time_to_unix_usec(created_new));
	}
#endif
	priv->desc.idVendor = json_object_get_int_member_with_default(json_object, "IdVendor", 0x0);
	priv->desc.idProduct =
	    json_object_get_int_member_with_default(json_object, "IdProduct", 0x0);
	priv->desc.bcdDevice = json_object_get_int_member_with_default(json_object, "Device", 0x0);
	priv->desc.bcdUSB = json_object_get_int_member_with_default(json_object, "USB", 0x0);
	priv->desc.iManufacturer =
	    json_object_get_int_member_with_default(json_object, "Manufacturer", 0x0);
	priv->desc.bDeviceClass =
	    json_object_get_int_member_with_default(json_object, "DeviceClass", 0x0);
	priv->desc.bDeviceSubClass =
	    json_object_get_int_member_with_default(json_object, "DeviceSubClass", 0x0);
	priv->desc.bDeviceProtocol =
	    json_object_get_int_member_with_default(json_object, "DeviceProtocol", 0x0);
	priv->desc.iProduct = json_object_get_int_member_with_default(json_object, "Product", 0x0);
	priv->desc.iSerialNumber =
	    json_object_get_int_member_with_default(json_object, "SerialNumber", 0x0);

	/* array of BOS descriptors */
	if (json_object_has_member(json_object, "UsbBosDescriptors")) {
		JsonArray *json_array =
		    json_object_get_array_member(json_object, "UsbBosDescriptors");
		for (guint i = 0; i < json_array_get_length(json_array); i++) {
			JsonNode *node_tmp = json_array_get_element(json_array, i);
			g_autoptr(FuUsbBosDescriptor) bos_descriptor =
			    g_object_new(FU_TYPE_USB_BOS_DESCRIPTOR, NULL);
			if (!fwupd_codec_from_json(FWUPD_CODEC(bos_descriptor), node_tmp, error))
				return FALSE;
			g_ptr_array_add(priv->bos_descriptors, g_object_ref(bos_descriptor));
		}
	}

	/* array of HID descriptors */
	if (json_object_has_member(json_object, "UsbHidDescriptors")) {
		JsonArray *json_array =
		    json_object_get_array_member(json_object, "UsbHidDescriptors");
		for (guint i = 0; i < json_array_get_length(json_array); i++) {
			JsonNode *node_tmp = json_array_get_element(json_array, i);
			tmp = json_node_get_string(node_tmp);
			if (tmp != NULL) {
				gsize bufsz = 0;
				g_autofree guchar *buf = g_base64_decode(tmp, &bufsz);
				g_ptr_array_add(priv->hid_descriptors,
						g_bytes_new_take(g_steal_pointer(&buf), bufsz));
			}
		}
	}

	/* array of interfaces */
	if (json_object_has_member(json_object, "UsbInterfaces")) {
		JsonArray *json_array = json_object_get_array_member(json_object, "UsbInterfaces");
		for (guint i = 0; i < json_array_get_length(json_array); i++) {
			JsonNode *node_tmp = json_array_get_element(json_array, i);
			g_autoptr(FuUsbInterface) iface = g_object_new(FU_TYPE_USB_INTERFACE, NULL);
			if (!fwupd_codec_from_json(FWUPD_CODEC(iface), node_tmp, error))
				return FALSE;
			fu_usb_device_add_interface_internal(self, iface);
		}
	}

	/* array of events */
	if (json_object_has_member(json_object, "UsbEvents")) {
		JsonArray *json_array = json_object_get_array_member(json_object, "UsbEvents");
		for (guint i = 0; i < json_array_get_length(json_array); i++) {
			JsonNode *node_tmp = json_array_get_element(json_array, i);
			g_autoptr(FuDeviceEvent) event = fu_device_event_new(NULL);
			if (!fwupd_codec_from_json(FWUPD_CODEC(event), node_tmp, error))
				return FALSE;
			fu_device_add_event(FU_DEVICE(self), event);
		}
	}

	/* success */
	priv->interfaces_valid = TRUE;
	priv->bos_descriptors_valid = TRUE;
	priv->hid_descriptors_valid = TRUE;
	return TRUE;
}

static void
fu_usb_device_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuUsbDevice *self = FU_USB_DEVICE(codec);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	GPtrArray *events = fu_device_get_events(FU_DEVICE(self));
	g_autoptr(GPtrArray) bos_descriptors = NULL;
	g_autoptr(GPtrArray) hid_descriptors = NULL;
	g_autoptr(GPtrArray) interfaces = NULL;
	g_autoptr(GError) error_bos = NULL;
	g_autoptr(GError) error_hid = NULL;
	g_autoptr(GError) error_interfaces = NULL;

	/* optional properties */
	fwupd_codec_json_append(builder, "GType", "FuUsbDevice");
	fwupd_codec_json_append(builder, "PlatformId", fu_device_get_physical_id(FU_DEVICE(self)));
#if GLIB_CHECK_VERSION(2, 80, 0)
	if (fu_device_get_created_usec(FU_DEVICE(self)) != 0) {
		g_autoptr(GDateTime) dt =
		    g_date_time_new_from_unix_utc_usec(fu_device_get_created_usec(FU_DEVICE(self)));
		g_autofree gchar *str = g_date_time_format_iso8601(dt);
		fwupd_codec_json_append(builder, "Created", str);
	}
#endif
	if (priv->desc.idVendor != 0)
		fwupd_codec_json_append_int(builder, "IdVendor", priv->desc.idVendor);
	if (priv->desc.idProduct != 0)
		fwupd_codec_json_append_int(builder, "IdProduct", priv->desc.idProduct);
	if (priv->desc.bcdDevice != 0)
		fwupd_codec_json_append_int(builder, "Device", priv->desc.bcdDevice);
	if (priv->desc.bcdUSB != 0)
		fwupd_codec_json_append_int(builder, "USB", priv->desc.bcdUSB);
	if (priv->desc.iManufacturer != 0)
		fwupd_codec_json_append_int(builder, "Manufacturer", priv->desc.iManufacturer);
	if (priv->desc.bDeviceClass != 0)
		fwupd_codec_json_append_int(builder, "DeviceClass", priv->desc.bDeviceClass);
	if (priv->desc.bDeviceSubClass != 0)
		fwupd_codec_json_append_int(builder, "DeviceSubClass", priv->desc.bDeviceSubClass);
	if (priv->desc.bDeviceProtocol != 0)
		fwupd_codec_json_append_int(builder, "DeviceProtocol", priv->desc.bDeviceProtocol);
	if (priv->desc.iProduct != 0)
		fwupd_codec_json_append_int(builder, "Product", priv->desc.iProduct);
	if (priv->desc.iSerialNumber != 0)
		fwupd_codec_json_append_int(builder, "SerialNumber", priv->desc.iSerialNumber);

	/* array of BOS descriptors */
	bos_descriptors = fu_usb_device_get_bos_descriptors(self, &error_bos);
	if (bos_descriptors == NULL) {
		g_debug("%s", error_bos->message);
	} else if (bos_descriptors->len > 0) {
		json_builder_set_member_name(builder, "UsbBosDescriptors");
		json_builder_begin_array(builder);
		for (guint i = 0; i < bos_descriptors->len; i++) {
			FuUsbBosDescriptor *bos_descriptor = g_ptr_array_index(bos_descriptors, i);
			json_builder_begin_object(builder);
			fwupd_codec_to_json(FWUPD_CODEC(bos_descriptor), builder, flags);
			json_builder_end_object(builder);
		}
		json_builder_end_array(builder);
	}

	/* array of HID descriptors */
	hid_descriptors = fu_usb_device_get_hid_descriptors(self, &error_hid);
	if (hid_descriptors == NULL) {
		g_debug("%s", error_hid->message);
	} else if (hid_descriptors->len > 0) {
		json_builder_set_member_name(builder, "UsbHidDescriptors");
		json_builder_begin_array(builder);
		for (guint i = 0; i < hid_descriptors->len; i++) {
			GBytes *bytes = g_ptr_array_index(hid_descriptors, i);
			g_autofree gchar *str =
			    g_base64_encode(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
			json_builder_add_string_value(builder, str);
		}
		json_builder_end_array(builder);
	}

	/* array of interfaces */
	interfaces = fu_usb_device_get_interfaces(self, &error_interfaces);
	if (interfaces == NULL) {
		g_debug("%s", error_interfaces->message);
	} else if (interfaces->len > 0) {
		json_builder_set_member_name(builder, "UsbInterfaces");
		json_builder_begin_array(builder);
		for (guint i = 0; i < interfaces->len; i++) {
			FuUsbInterface *iface = g_ptr_array_index(interfaces, i);
			json_builder_begin_object(builder);
			fwupd_codec_to_json(FWUPD_CODEC(iface), builder, flags);
			json_builder_end_object(builder);
		}
		json_builder_end_array(builder);
	}

	/* events */
	if (events->len > 0) {
		json_builder_set_member_name(builder, "UsbEvents");
		json_builder_begin_array(builder);
		for (guint i = 0; i < events->len; i++) {
			FuDeviceEvent *event = g_ptr_array_index(events, i);
			json_builder_begin_object(builder);
			fwupd_codec_to_json(FWUPD_CODEC(event), builder, flags);
			json_builder_end_object(builder);
		}
		json_builder_end_array(builder);
	}
}

static void
fu_usb_device_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_usb_device_add_json;
	iface->from_json = fu_usb_device_from_json;
}

/**
 * fu_usb_device_new: (skip):
 * @ctx: (nullable): a #FuContext
 * @usb_device: a #libusb_device
 *
 * Creates a new #FuUsbDevice.
 *
 * Returns: (transfer full): a #FuUsbDevice
 *
 * Since: 2.0.0
 **/
FuUsbDevice *
fu_usb_device_new(FuContext *ctx, libusb_device *usb_device)
{
	return g_object_new(FU_TYPE_USB_DEVICE, "context", ctx, "libusb-device", usb_device, NULL);
}

static void
fu_usb_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);

	if (priv->configuration >= 0)
		fwupd_codec_string_append_hex(str, idt, "Configuration", priv->configuration);
	fwupd_codec_string_append_hex(str, idt, "ClaimRetryCount", priv->claim_retry_count);
	fwupd_codec_string_append_hex(str, idt, "OpenRetryCount", priv->open_retry_count);
	fwupd_codec_string_append_hex(str, idt, "BusNum", priv->busnum);
	fwupd_codec_string_append_hex(str, idt, "DevNum", priv->devnum);
	for (guint i = 0; priv->device_interfaces != NULL && i < priv->device_interfaces->len;
	     i++) {
		FuUsbDeviceInterface *iface = g_ptr_array_index(priv->device_interfaces, i);
		g_autofree gchar *tmp = g_strdup_printf("InterfaceNumber#%02x", iface->number);
		fwupd_codec_string_append(str, idt, tmp, iface->claimed ? "claimed" : "released");
	}
	fwupd_codec_string_append(str,
				  idt,
				  "Class",
				  fu_usb_class_to_string(fu_usb_device_get_class(self)));
	if (priv->interfaces->len > 0) {
		fwupd_codec_string_append(str, idt, "Interfaces", "");
		for (guint i = 0; i < priv->interfaces->len; i++) {
			FuUsbInterface *iface = g_ptr_array_index(priv->interfaces, i);
			fwupd_codec_add_string(FWUPD_CODEC(iface), idt + 1, str);
		}
	}
	if (priv->bos_descriptors->len > 0) {
		fwupd_codec_string_append(str, idt, "BosDescriptors", "");
		for (guint i = 0; i < priv->bos_descriptors->len; i++) {
			FuUsbBosDescriptor *bos_descriptor =
			    g_ptr_array_index(priv->bos_descriptors, i);
			fwupd_codec_add_string(FWUPD_CODEC(bos_descriptor), idt + 1, str);
		}
	}
	if (priv->hid_descriptors->len > 0) {
		fwupd_codec_string_append(str, idt, "HidDescriptors", "");
		for (guint i = 0; i < priv->hid_descriptors->len; i++) {
			GBytes *hid_descriptor = g_ptr_array_index(priv->hid_descriptors, i);
			g_autofree gchar *key = g_strdup_printf("HidDescriptor0x%02u", i);
			fwupd_codec_string_append_hex(str,
						      idt + 1,
						      key,
						      g_bytes_get_size(hid_descriptor));
		}
	}
}

static void
fu_usb_device_class_init(FuUsbDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fu_usb_device_finalize;
	object_class->get_property = fu_usb_device_get_property;
	object_class->set_property = fu_usb_device_set_property;
	device_class->open = fu_usb_device_open;
	device_class->setup = fu_usb_device_setup;
	device_class->ready = fu_usb_device_ready;
	device_class->close = fu_usb_device_close;
	device_class->probe = fu_usb_device_probe;
	device_class->invalidate = fu_usb_device_invalidate;
	device_class->to_string = fu_usb_device_to_string;
	device_class->incorporate = fu_usb_device_incorporate;
	device_class->bind_driver = fu_udev_device_bind_driver;
	device_class->unbind_driver = fu_udev_device_unbind_driver;
	device_class->convert_version = fu_usb_device_convert_version;

	/**
	 * FuUsbDevice:libusb-device:
	 *
	 * The low-level #libusb_device.
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_pointer("libusb-device",
				     NULL,
				     NULL,
				     G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
	g_object_class_install_property(object_class, PROP_LIBUSB_DEVICE, pspec);
}
