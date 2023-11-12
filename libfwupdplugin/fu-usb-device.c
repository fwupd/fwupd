/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuUsbDevice"

#include "config.h"

#include "fu-device-private.h"
#include "fu-dump.h"
#include "fu-mem.h"
#include "fu-string.h"
#include "fu-usb-device-fw-ds20.h"
#include "fu-usb-device-ms-ds20.h"
#include "fu-usb-device-private.h"

/**
 * FuUsbDevice:
 *
 * A USB device.
 *
 * See also: [class@FuDevice], [class@FuHidDevice]
 */

typedef struct {
	GUsbDevice *usb_device;
	gint configuration;
	GPtrArray *interfaces; /* nullable, element-type FuUsbDeviceInterface */
	FuDeviceLocker *usb_device_locker;
} FuUsbDevicePrivate;

typedef struct {
	guint8 number;
	gboolean claimed;
} FuUsbDeviceInterface;

G_DEFINE_TYPE_WITH_PRIVATE(FuUsbDevice, fu_usb_device, FU_TYPE_DEVICE)
enum { PROP_0, PROP_USB_DEVICE, PROP_LAST };

#define GET_PRIVATE(o) (fu_usb_device_get_instance_private(o))

#define FU_USB_DEVICE_CLAIM_INTERFACE_RETRIES 5
#define FU_USB_DEVICE_CLAIM_INTERFACE_DELAY   500 /* ms */

#define FU_USB_DEVICE_OPEN_RETRIES 5
#define FU_USB_DEVICE_OPEN_DELAY   50 /* ms */

static void
fu_usb_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuUsbDevice *device = FU_USB_DEVICE(object);
	FuUsbDevicePrivate *priv = GET_PRIVATE(device);
	switch (prop_id) {
	case PROP_USB_DEVICE:
		g_value_set_object(value, priv->usb_device);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_usb_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuUsbDevice *device = FU_USB_DEVICE(object);
	switch (prop_id) {
	case PROP_USB_DEVICE:
		fu_usb_device_set_dev(device, g_value_get_object(value));
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

	if (priv->usb_device_locker != NULL)
		g_object_unref(priv->usb_device_locker);
	if (priv->usb_device != NULL)
		g_object_unref(priv->usb_device);
	if (priv->interfaces != NULL)
		g_ptr_array_unref(priv->interfaces);

	G_OBJECT_CLASS(fu_usb_device_parent_class)->finalize(object);
}

#if G_USB_CHECK_VERSION(0, 4, 5)
static void
fu_usb_device_flags_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	if (usb_device == NULL)
		return;
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG))
		g_usb_device_add_tag(usb_device, FU_USB_DEVICE_EMULATION_TAG);
}
#endif

static void
fu_usb_device_init(FuUsbDevice *device)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(device);
	priv->configuration = -1;
	fu_device_set_acquiesce_delay(FU_DEVICE(device), 2500);
#ifdef HAVE_GUSB
	fu_device_retry_add_recovery(FU_DEVICE(device),
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_NO_DEVICE,
				     NULL);
	fu_device_retry_add_recovery(FU_DEVICE(device),
				     G_USB_DEVICE_ERROR,
				     G_USB_DEVICE_ERROR_PERMISSION_DENIED,
				     NULL);
#endif
}

static void
fu_usb_device_constructed(GObject *obj)
{
#if G_USB_CHECK_VERSION(0, 4, 5)
	FuUsbDevice *self = FU_USB_DEVICE(obj);
	/* copy this to the GUsbDevice */
	g_signal_connect(FU_DEVICE(self),
			 "notify::flags",
			 G_CALLBACK(fu_usb_device_flags_notify_cb),
			 NULL);
#endif
}

/**
 * fu_usb_device_is_open:
 * @device: a #FuUsbDevice
 *
 * Finds out if a USB device is currently open.
 *
 * Returns: %TRUE if the device is open.
 *
 * Since: 1.0.3
 **/
gboolean
fu_usb_device_is_open(FuUsbDevice *device)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(device);
	g_return_val_if_fail(FU_IS_USB_DEVICE(device), FALSE);
	return priv->usb_device_locker != NULL;
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

	if (priv->interfaces == NULL)
		priv->interfaces = g_ptr_array_new_with_free_func(g_free);

	/* check for existing */
	for (guint i = 0; i < priv->interfaces->len; i++) {
		iface = g_ptr_array_index(priv->interfaces, i);
		if (iface->number == number)
			return;
	}

	/* add new */
	iface = g_new0(FuUsbDeviceInterface, 1);
	iface->number = number;
	g_ptr_array_add(priv->interfaces, iface);
}

#ifdef HAVE_GUSB
static gboolean
fu_usb_device_query_hub(FuUsbDevice *self, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	gsize sz = 0;
	guint16 value = 0x29;
	guint8 data[0x0c] = {0x0};
	g_autoptr(GString) hub = g_string_new(NULL);

	/* longer descriptor for SuperSpeed */
	if (fu_usb_device_get_spec(self) >= 0x0300)
		value = 0x2a;
	if (!g_usb_device_control_transfer(priv->usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_CLASS,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
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
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	FuUsbDeviceInterface *iface = (FuUsbDeviceInterface *)user_data;
	return g_usb_device_claim_interface(priv->usb_device,
					    iface->number,
					    G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					    error);
}

static gboolean
fu_usb_device_open_internal_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	return g_usb_device_open(priv->usb_device, error);
}

static gboolean
fu_usb_device_open_internal(FuUsbDevice *self, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_usb_device_open_internal_cb,
				    FU_USB_DEVICE_OPEN_RETRIES,
				    FU_USB_DEVICE_OPEN_DELAY,
				    NULL,
				    error);
}

static gboolean
fu_usb_device_close_internal(FuUsbDevice *self, GError **error)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	return g_usb_device_close(priv->usb_device, error);
}
#endif

static gboolean
fu_usb_device_open(FuDevice *device, GError **error)
{
#ifdef HAVE_GUSB
	FuUsbDevice *self = FU_USB_DEVICE(device);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuDeviceLocker) locker = NULL;

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* already open */
	if (priv->usb_device_locker != NULL)
		return TRUE;

	/* open */
	locker = fu_device_locker_new_full(self,
					   (FuDeviceLockerFunc)fu_usb_device_open_internal,
					   (FuDeviceLockerFunc)fu_usb_device_close_internal,
					   error);
	if (locker == NULL) {
		g_prefix_error(error, "failed to open device: ");
		return FALSE;
	}

	/* success */
	priv->usb_device_locker = g_steal_pointer(&locker);

	/* if set */
	if (priv->configuration >= 0) {
		if (!g_usb_device_set_configuration(priv->usb_device, priv->configuration, error)) {
			g_prefix_error(error, "failed to set configuration: ");
			return FALSE;
		}
	}

	/* claim interfaces */
	for (guint i = 0; priv->interfaces != NULL && i < priv->interfaces->len; i++) {
		FuUsbDeviceInterface *iface = g_ptr_array_index(priv->interfaces, i);
		if (!fu_device_retry_full(device,
					  fu_usb_device_claim_interface_cb,
					  FU_USB_DEVICE_CLAIM_INTERFACE_RETRIES,
					  FU_USB_DEVICE_CLAIM_INTERFACE_DELAY,
					  iface,
					  error)) {
			g_prefix_error(error, "failed to claim interface 0x%02x: ", iface->number);
			return FALSE;
		}
		iface->claimed = TRUE;
	}
#endif
	return TRUE;
}

static gboolean
fu_usb_device_setup(FuDevice *device, GError **error)
{
#ifdef HAVE_GUSB
	FuUsbDevice *self = FU_USB_DEVICE(device);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	guint idx;
#if G_USB_CHECK_VERSION(0, 4, 0)
	g_autoptr(GPtrArray) bos_descriptors = NULL;
#endif

	g_return_val_if_fail(FU_IS_USB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* get vendor */
	if (fu_device_get_vendor(device) == NULL) {
		idx = g_usb_device_get_manufacturer_index(priv->usb_device);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			g_autoptr(GError) error_local = NULL;
			tmp =
			    g_usb_device_get_string_descriptor(priv->usb_device, idx, &error_local);
			if (tmp != NULL)
				fu_device_set_vendor(device, g_strchomp(tmp));
			else
				g_debug(
				    "failed to load manufacturer string for usb device %u:%u: %s",
				    g_usb_device_get_bus(priv->usb_device),
				    g_usb_device_get_address(priv->usb_device),
				    error_local->message);
		}
	}

	/* get product */
	if (fu_device_get_name(device) == NULL) {
		idx = g_usb_device_get_product_index(priv->usb_device);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			g_autoptr(GError) error_local = NULL;
			tmp =
			    g_usb_device_get_string_descriptor(priv->usb_device, idx, &error_local);
			if (tmp != NULL)
				fu_device_set_name(device, g_strchomp(tmp));
			else
				g_debug("failed to load product string for usb device %u:%u: %s",
					g_usb_device_get_bus(priv->usb_device),
					g_usb_device_get_address(priv->usb_device),
					error_local->message);
		}
	}

	/* get serial number */
	if (!fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_NO_SERIAL_NUMBER) &&
	    fu_device_get_serial(device) == NULL) {
		idx = g_usb_device_get_serial_number_index(priv->usb_device);
		if (idx != 0x00) {
			g_autofree gchar *tmp = NULL;
			g_autoptr(GError) error_local = NULL;
			tmp =
			    g_usb_device_get_string_descriptor(priv->usb_device, idx, &error_local);
			if (tmp != NULL)
				fu_device_set_serial(device, g_strchomp(tmp));
			else
				g_debug(
				    "failed to load serial number string for usb device %u:%u: %s",
				    g_usb_device_get_bus(priv->usb_device),
				    g_usb_device_get_address(priv->usb_device),
				    error_local->message);
		}
	}

	/* get the hub descriptor if this is a hub */
	if (g_usb_device_get_device_class(priv->usb_device) == G_USB_DEVICE_CLASS_HUB) {
		if (!fu_usb_device_query_hub(self, error))
			return FALSE;
	}

#if G_USB_CHECK_VERSION(0, 4, 2)
	/* get the platform capability BOS descriptors */
	bos_descriptors = g_usb_device_get_bos_descriptors(priv->usb_device, NULL);
	for (guint i = 0; bos_descriptors != NULL && i < bos_descriptors->len; i++) {
		GUsbBosDescriptor *bos = g_ptr_array_index(bos_descriptors, i);
		GBytes *extra = g_usb_bos_descriptor_get_extra(bos);
		if (g_usb_bos_descriptor_get_capability(bos) == 0x5 &&
		    g_bytes_get_size(extra) > 0) {
			g_autoptr(FuFirmware) ds20 = NULL;
			g_autofree gchar *str = NULL;
			g_autoptr(GError) error_ds20 = NULL;

			ds20 = fu_firmware_new_from_gtypes(extra,
							   0x0,
							   FWUPD_INSTALL_FLAG_NONE,
							   &error_ds20,
							   FU_TYPE_USB_DEVICE_FW_DS20,
							   FU_TYPE_USB_DEVICE_MS_DS20,
							   G_TYPE_INVALID);
			if (ds20 == NULL) {
				g_warning("failed to parse platform capability BOS descriptor: %s",
					  error_ds20->message);
				continue;
			}
			if (!fu_usb_device_ds20_apply_to_device(FU_USB_DEVICE_DS20(ds20),
								self,
								&error_ds20)) {
				g_warning("failed to get DS20 data: %s", error_ds20->message);
				continue;
			}
			str = fu_firmware_to_string(ds20);
			g_debug("DS20: %s", str);
		}
	}
#endif
#endif

	/* success */
	return TRUE;
}

static gboolean
fu_usb_device_ready(FuDevice *device, GError **error)
{
#ifdef HAVE_GUSB
	FuUsbDevice *self = FU_USB_DEVICE(device);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) intfs = NULL;

	/* get the interface GUIDs */
	intfs = g_usb_device_get_interfaces(priv->usb_device, error);
	if (intfs == NULL) {
		g_prefix_error(error, "failed to get interfaces: ");
		return FALSE;
	}

	/* add fallback icon if there is nothing added already */
	if (fu_device_get_icons(device)->len == 0) {
		for (guint i = 0; i < intfs->len; i++) {
			GUsbInterface *intf = g_ptr_array_index(intfs, i);

			/* Video: Video Control: i.e. a webcam */
			if (g_usb_interface_get_class(intf) == G_USB_DEVICE_CLASS_VIDEO &&
			    g_usb_interface_get_subclass(intf) == 0x01) {
				fu_device_add_icon(device, "camera-web");
			}

			/* Audio */
			if (g_usb_interface_get_class(intf) == G_USB_DEVICE_CLASS_AUDIO)
				fu_device_add_icon(device, "audio-card");

			/* Mass Storage */
			if (g_usb_interface_get_class(intf) == G_USB_DEVICE_CLASS_MASS_STORAGE)
				fu_device_add_icon(device, "drive-harddisk");

			/* Printer */
			if (g_usb_interface_get_class(intf) == G_USB_DEVICE_CLASS_PRINTER)
				fu_device_add_icon(device, "printer");
		}
	}
#endif

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

	/* already closed */
	if (priv->usb_device_locker == NULL)
		return TRUE;

#ifdef HAVE_GUSB
	/* release interfaces, ignoring errors */
	for (guint i = 0; priv->interfaces != NULL && i < priv->interfaces->len; i++) {
		FuUsbDeviceInterface *iface = g_ptr_array_index(priv->interfaces, i);
		GUsbDeviceClaimInterfaceFlags claim_flags = G_USB_DEVICE_CLAIM_INTERFACE_NONE;
		g_autoptr(GError) error_local = NULL;
		if (!iface->claimed)
			continue;
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG)) {
			g_debug("re-binding kernel driver as not waiting for replug");
			claim_flags |= G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER;
		}
		if (!g_usb_device_release_interface(priv->usb_device,
						    iface->number,
						    claim_flags,
						    &error_local)) {
			if (g_error_matches(error_local,
					    G_USB_DEVICE_ERROR,
					    G_USB_DEVICE_ERROR_NO_DEVICE) ||
			    g_error_matches(error_local,
					    G_USB_DEVICE_ERROR,
					    G_USB_DEVICE_ERROR_INTERNAL)) {
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
#endif

	g_clear_object(&priv->usb_device_locker);
	return TRUE;
}

static gboolean
fu_usb_device_probe(FuDevice *device, GError **error)
{
#ifdef HAVE_GUSB
	FuUsbDevice *self = FU_USB_DEVICE(device);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	guint16 release;
	g_autofree gchar *platform_id = NULL;
	g_autofree gchar *vendor_id = NULL;
	g_autoptr(GPtrArray) intfs = NULL;

	/* set vendor ID */
	vendor_id = g_strdup_printf("USB:0x%04X", g_usb_device_get_vid(priv->usb_device));
	fu_device_add_vendor_id(device, vendor_id);

	/* set the version if the release has been set */
	release = g_usb_device_get_release(priv->usb_device);
	if (release != 0x0 &&
	    fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_BCD);
		fu_device_set_version_u16(device, release);
	}

	/* add GUIDs in order of priority */
	fu_device_add_instance_u16(device, "VID", g_usb_device_get_vid(priv->usb_device));
	fu_device_add_instance_u16(device, "PID", g_usb_device_get_pid(priv->usb_device));
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
	intfs = g_usb_device_get_interfaces(priv->usb_device, error);
	if (intfs == NULL) {
		g_prefix_error(error, "failed to get interfaces: ");
		return FALSE;
	}
	for (guint i = 0; i < intfs->len; i++) {
		GUsbInterface *intf = g_ptr_array_index(intfs, i);
		fu_device_add_instance_u8(device, "CLASS", g_usb_interface_get_class(intf));
		fu_device_add_instance_u8(device, "SUBCLASS", g_usb_interface_get_subclass(intf));
		fu_device_add_instance_u8(device, "PROT", g_usb_interface_get_protocol(intf));
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
	platform_id = g_strdup(g_usb_device_get_platform_id(priv->usb_device));
	for (guint i = 0; i < 2; i++) {
		gchar *tok = g_strrstr(platform_id, ":");
		if (tok == NULL)
			break;
		*tok = '\0';
		if (g_strcmp0(platform_id, "usb") == 0)
			break;
		fu_device_add_parent_physical_id(device, platform_id);
	}
#endif

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
#ifdef HAVE_GUSB
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), 0x0000);
	if (priv->usb_device == NULL)
		return 0x0;
	return g_usb_device_get_vid(priv->usb_device);
#else
	return 0x0;
#endif
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
#ifdef HAVE_GUSB
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), 0x0000);
	if (priv->usb_device == NULL)
		return 0x0;
	return g_usb_device_get_pid(priv->usb_device);
#else
	return 0x0;
#endif
}

/**
 * fu_usb_device_get_platform_id:
 * @self: a #FuUsbDevice
 *
 * Gets the device platform ID.
 *
 * Returns: string, or NULL if unset or invalid
 *
 * Since: 1.1.2
 **/
const gchar *
fu_usb_device_get_platform_id(FuUsbDevice *self)
{
#ifdef HAVE_GUSB
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), NULL);
	if (priv->usb_device == NULL)
		return NULL;
	return g_usb_device_get_platform_id(priv->usb_device);
#else
	return NULL;
#endif
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
#ifdef HAVE_GUSB
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_USB_DEVICE(self), 0x0);
	if (priv->usb_device == NULL)
		return 0x0;
	return g_usb_device_get_spec(priv->usb_device);
#else
	return 0x0;
#endif
}

/**
 * fu_usb_device_set_dev:
 * @device: a #FuUsbDevice
 * @usb_device: (nullable): optional #GUsbDevice
 *
 * Sets the #GUsbDevice to use.
 *
 * Since: 1.0.2
 **/
void
fu_usb_device_set_dev(FuUsbDevice *device, GUsbDevice *usb_device)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(device);

	g_return_if_fail(FU_IS_USB_DEVICE(device));

	/* need to re-probe hardware */
	if (!fu_device_has_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_EMULATED))
		fu_device_probe_invalidate(FU_DEVICE(device));

	/* allow replacement */
	g_set_object(&priv->usb_device, usb_device);
	if (usb_device == NULL) {
		g_clear_object(&priv->usb_device_locker);
		return;
	}

#ifdef HAVE_GUSB
#if G_USB_CHECK_VERSION(0, 4, 5)
	/* propagate emulated flag */
	if (usb_device != NULL && g_usb_device_is_emulated(usb_device))
		fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_EMULATED);
#endif

	/* set device ID automatically */
	fu_device_set_physical_id(FU_DEVICE(device), g_usb_device_get_platform_id(usb_device));
#endif
}

/**
 * fu_usb_device_find_udev_device:
 * @device: a #FuUsbDevice
 * @error: (nullable): optional return location for an error
 *
 * Gets the matching #GUdevDevice for the #GUsbDevice.
 *
 * Returns: (transfer full): a #GUdevDevice, or NULL if unset or invalid
 *
 * Since: 1.3.2
 **/
GUdevDevice *
fu_usb_device_find_udev_device(FuUsbDevice *device, GError **error)
{
#if defined(HAVE_GUDEV) && defined(HAVE_GUSB)
	FuUsbDevicePrivate *priv = GET_PRIVATE(device);
	g_autoptr(GList) devices = NULL;
	g_autoptr(GUdevClient) gudev_client = g_udev_client_new(NULL);

	g_return_val_if_fail(FU_IS_USB_DEVICE(device), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* find all tty devices */
	devices = g_udev_client_query_by_subsystem(gudev_client, "usb");
	for (GList *l = devices; l != NULL; l = l->next) {
		GUdevDevice *dev = G_UDEV_DEVICE(l->data);

		/* check correct device */
		if (g_udev_device_get_sysfs_attr_as_int(dev, "busnum") !=
		    g_usb_device_get_bus(priv->usb_device))
			continue;
		if (g_udev_device_get_sysfs_attr_as_int(dev, "devnum") !=
		    g_usb_device_get_address(priv->usb_device))
			continue;

		/* success */
		g_debug("USB device %u:%u is %s",
			g_usb_device_get_bus(priv->usb_device),
			g_usb_device_get_address(priv->usb_device),
			g_udev_device_get_sysfs_path(dev));
		return g_object_ref(dev);
	}

	/* failure */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "could not find sysfs device for %u:%u",
		    g_usb_device_get_bus(priv->usb_device),
		    g_usb_device_get_address(priv->usb_device));
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <gudev.h> is unavailable");
#endif
	return NULL;
}

/**
 * fu_usb_device_get_dev:
 * @device: a #FuUsbDevice
 *
 * Gets the #GUsbDevice.
 *
 * Returns: (transfer none): a USB device, or %NULL
 *
 * Since: 1.0.2
 **/
GUsbDevice *
fu_usb_device_get_dev(FuUsbDevice *device)
{
	FuUsbDevicePrivate *priv = GET_PRIVATE(device);
	g_return_val_if_fail(FU_IS_USB_DEVICE(device), NULL);
	return priv->usb_device;
}

static void
fu_usb_device_incorporate(FuDevice *self, FuDevice *donor)
{
	g_return_if_fail(FU_IS_USB_DEVICE(self));
	g_return_if_fail(FU_IS_USB_DEVICE(donor));
	fu_usb_device_set_dev(FU_USB_DEVICE(self), fu_usb_device_get_dev(FU_USB_DEVICE(donor)));
}

static gboolean
fu_udev_device_bind_driver(FuDevice *device,
			   const gchar *subsystem,
			   const gchar *driver,
			   GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	g_autoptr(GUdevDevice) dev = NULL;
	g_autoptr(FuUdevDevice) udev_device = NULL;

	/* use udev for this */
	dev = fu_usb_device_find_udev_device(self, error);
	if (dev == NULL)
		return FALSE;
	udev_device = fu_udev_device_new(fu_device_get_context(device), dev);
	return fu_device_bind_driver(FU_DEVICE(udev_device), subsystem, driver, error);
}

static gboolean
fu_udev_device_unbind_driver(FuDevice *device, GError **error)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	g_autoptr(GUdevDevice) dev = NULL;
	g_autoptr(FuUdevDevice) udev_device = NULL;

	/* use udev for this */
	dev = fu_usb_device_find_udev_device(self, error);
	if (dev == NULL)
		return FALSE;
	udev_device = fu_udev_device_new(fu_device_get_context(device), dev);
	return fu_device_unbind_driver(FU_DEVICE(udev_device), error);
}

/**
 * fu_usb_device_new:
 * @ctx: (nullable): a #FuContext
 * @usb_device: a USB device
 *
 * Creates a new #FuUsbDevice.
 *
 * Returns: (transfer full): a #FuUsbDevice
 *
 * Since: 1.8.2
 **/
FuUsbDevice *
fu_usb_device_new(FuContext *ctx, GUsbDevice *usb_device)
{
#if G_USB_CHECK_VERSION(0, 4, 3)
	if (usb_device != NULL && g_usb_device_has_tag(usb_device, "is-transient")) {
		g_critical("cannot use a device built using fu_udev_device_find_usb_device() as "
			   "the GUsbContext is different");
		return NULL;
	}
#endif
	return g_object_new(FU_TYPE_USB_DEVICE, "context", ctx, "usb-device", usb_device, NULL);
}

#ifdef HAVE_GUSB
static const gchar *
fu_usb_device_class_code_to_string(GUsbDeviceClassCode code)
{
	if (code == G_USB_DEVICE_CLASS_INTERFACE_DESC)
		return "interface-desc";
	if (code == G_USB_DEVICE_CLASS_AUDIO)
		return "audio";
	if (code == G_USB_DEVICE_CLASS_COMMUNICATIONS)
		return "communications";
	if (code == G_USB_DEVICE_CLASS_HID)
		return "hid";
	if (code == G_USB_DEVICE_CLASS_PHYSICAL)
		return "physical";
	if (code == G_USB_DEVICE_CLASS_IMAGE)
		return "image";
	if (code == G_USB_DEVICE_CLASS_PRINTER)
		return "printer";
	if (code == G_USB_DEVICE_CLASS_MASS_STORAGE)
		return "mass-storage";
	if (code == G_USB_DEVICE_CLASS_HUB)
		return "hub";
	if (code == G_USB_DEVICE_CLASS_CDC_DATA)
		return "cdc-data";
	if (code == G_USB_DEVICE_CLASS_SMART_CARD)
		return "smart-card";
	if (code == G_USB_DEVICE_CLASS_CONTENT_SECURITY)
		return "content-security";
	if (code == G_USB_DEVICE_CLASS_VIDEO)
		return "video";
	if (code == G_USB_DEVICE_CLASS_PERSONAL_HEALTHCARE)
		return "personal-healthcare";
	if (code == G_USB_DEVICE_CLASS_AUDIO_VIDEO)
		return "audio-video";
	if (code == G_USB_DEVICE_CLASS_BILLBOARD)
		return "billboard";
	if (code == G_USB_DEVICE_CLASS_DIAGNOSTIC)
		return "diagnostic";
	if (code == G_USB_DEVICE_CLASS_WIRELESS_CONTROLLER)
		return "wireless-controller";
	if (code == G_USB_DEVICE_CLASS_MISCELLANEOUS)
		return "miscellaneous";
	if (code == G_USB_DEVICE_CLASS_APPLICATION_SPECIFIC)
		return "application-specific";
	if (code == G_USB_DEVICE_CLASS_VENDOR_SPECIFIC)
		return "vendor-specific";
	return NULL;
}
#endif

static void
fu_usb_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUsbDevice *self = FU_USB_DEVICE(device);
	FuUsbDevicePrivate *priv = GET_PRIVATE(self);

	if (priv->configuration > 0)
		fu_string_append_kx(str, idt, "Configuration", priv->configuration);
	for (guint i = 0; priv->interfaces != NULL && i < priv->interfaces->len; i++) {
		FuUsbDeviceInterface *iface = g_ptr_array_index(priv->interfaces, i);
		g_autofree gchar *tmp = g_strdup_printf("InterfaceNumber#%02x", iface->number);
		fu_string_append(str, idt, tmp, iface->claimed ? "claimed" : "released");
	}

#ifdef HAVE_GUSB
	if (priv->usb_device != NULL) {
		GUsbDeviceClassCode code = g_usb_device_get_device_class(priv->usb_device);
		fu_string_append(str,
				 idt,
				 "UsbDeviceClass",
				 fu_usb_device_class_code_to_string(code));
	}
#endif
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
	object_class->constructed = fu_usb_device_constructed;
	device_class->open = fu_usb_device_open;
	device_class->setup = fu_usb_device_setup;
	device_class->ready = fu_usb_device_ready;
	device_class->close = fu_usb_device_close;
	device_class->probe = fu_usb_device_probe;
	device_class->to_string = fu_usb_device_to_string;
	device_class->incorporate = fu_usb_device_incorporate;
	device_class->bind_driver = fu_udev_device_bind_driver;
	device_class->unbind_driver = fu_udev_device_unbind_driver;

	/**
	 * FuUsbDevice:usb-device:
	 *
	 * The low-level #GUsbDevice.
	 *
	 * Since: 1.0.2
	 */
	pspec = g_param_spec_object("usb-device",
				    NULL,
				    NULL,
#ifdef HAVE_GUSB
				    G_USB_TYPE_DEVICE,
#else
				    G_TYPE_OBJECT,
#endif
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_USB_DEVICE, pspec);
}
