/*
 * Copyright (C) 2022 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-corsair-bp.h"
#include "fu-corsair-common.h"
#include "fu-corsair-device.h"

#define CORSAIR_DEFAULT_VENDOR_INTERFACE_ID 1

#define CORSAIR_TRANSACTION_TIMEOUT	    4000
#define CORSAIR_SUBDEVICE_POLL_PERIOD	    30000
#define CORSAIR_SUBDEVICE_REBOOT_DELAY	    (4 * G_USEC_PER_SEC)
#define CORSAIR_SUBDEVICE_RECONNECT_RETRIES 30
#define CORSAIR_SUBDEVICE_RECONNECT_PERIOD  1000

struct _FuCorsairDevice {
	FuUsbDevice parent_instance;
	FuCorsairDeviceKind device_kind;
	guint8 vendor_interface;
	gchar *subdevice_id;
	FuCorsairBp *bp;
};
G_DEFINE_TYPE(FuCorsairDevice, fu_corsair_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_corsair_device_probe(FuDevice *device, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	GUsbInterface *iface = NULL;
	GUsbEndpoint *ep1 = NULL;
	GUsbEndpoint *ep2 = NULL;
	g_autoptr(GPtrArray) ifaces = NULL;
	g_autoptr(GPtrArray) endpoints = NULL;
	g_autoptr(FuCorsairBp) bp = NULL;
	guint16 cmd_write_size;
	guint16 cmd_read_size;
	guint8 epin;
	guint8 epout;

	/* probing are skipped for subdevices */
	if (fu_device_has_private_flag(device, FU_CORSAIR_DEVICE_FLAG_IS_SUBDEVICE))
		return TRUE;

	if (!FU_DEVICE_CLASS(fu_corsair_device_parent_class)->probe(device, error))
		return FALSE;

	ifaces = g_usb_device_get_interfaces(usb_device, error);
	if (ifaces == NULL || (ifaces->len < (self->vendor_interface + 1u))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "update interface not found");
		return FALSE;
	}

	iface = g_ptr_array_index(ifaces, self->vendor_interface);
	endpoints = g_usb_interface_get_endpoints(iface);
	/* expecting to have two endpoints for communication */
	if (endpoints == NULL || endpoints->len != 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "update interface endpoints not found");
		return FALSE;
	}

	ep1 = g_ptr_array_index(endpoints, 0);
	ep2 = g_ptr_array_index(endpoints, 1);
	if (g_usb_endpoint_get_direction(ep1) == G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST) {
		epin = g_usb_endpoint_get_address(ep1);
		epout = g_usb_endpoint_get_address(ep2);
		cmd_read_size = g_usb_endpoint_get_maximum_packet_size(ep1);
		cmd_write_size = g_usb_endpoint_get_maximum_packet_size(ep2);
	} else {
		epin = g_usb_endpoint_get_address(ep2);
		epout = g_usb_endpoint_get_address(ep1);
		cmd_read_size = g_usb_endpoint_get_maximum_packet_size(ep2);
		cmd_write_size = g_usb_endpoint_get_maximum_packet_size(ep1);
	}

	if (cmd_write_size > FU_CORSAIR_MAX_CMD_SIZE || cmd_read_size > FU_CORSAIR_MAX_CMD_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "endpoint size is bigger than allowed command size");
		return FALSE;
	}

	fu_usb_device_add_interface(FU_USB_DEVICE(self), self->vendor_interface);

	self->bp = fu_corsair_bp_new(usb_device, FALSE);
	fu_corsair_bp_set_cmd_size(self->bp, cmd_write_size, cmd_read_size);
	fu_corsair_bp_set_endpoints(self->bp, epin, epout);

	return TRUE;
}

static gboolean
fu_corsair_poll_subdevice(FuDevice *device, gboolean *subdevice_added, GError **error)
{
	guint32 subdevices;
	g_autoptr(FuCorsairDevice) child = NULL;
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	g_autoptr(FuCorsairBp) child_bp = NULL;
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));

	if (!fu_corsair_bp_get_property(self->bp,
					FU_CORSAIR_BP_PROPERTY_SUBDEVICES,
					&subdevices,
					error)) {
		g_prefix_error(error, "cannot get subdevices: ");
		return FALSE;
	}

	if (subdevices == 0) {
		*subdevice_added = FALSE;
		return TRUE;
	}

	child_bp = fu_corsair_bp_new(usb_device, TRUE);
	fu_device_incorporate(FU_DEVICE(child_bp), FU_DEVICE(self->bp));

	child = fu_corsair_device_new(self, child_bp);
	fu_device_add_instance_id(FU_DEVICE(child), self->subdevice_id);
	fu_device_set_physical_id(FU_DEVICE(child), fu_device_get_physical_id(device));
	fu_device_set_logical_id(FU_DEVICE(child), "subdevice");
	fu_device_add_internal_flag(FU_DEVICE(child), FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN);

	if (!fu_device_probe(FU_DEVICE(child), error))
		return FALSE;
	if (!fu_device_setup(FU_DEVICE(child), error))
		return FALSE;

	fu_device_add_child(device, FU_DEVICE(child));
	*subdevice_added = TRUE;

	return TRUE;
}

static gchar *
fu_corsair_device_get_version(FuDevice *device, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	guint32 version_raw;

	if (!fu_corsair_bp_get_property(self->bp,
					FU_CORSAIR_BP_PROPERTY_VERSION,
					&version_raw,
					error))
		return NULL;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		gboolean broken_by_flag =
		    fu_device_has_private_flag(device,
					       FU_CORSAIR_DEVICE_FLAG_NO_VERSION_IN_BOOTLOADER);

		/* Version 0xffffffff means that previous update was interrupted.
		   Set version to 0.0.0 in both broken and interrupted cases to make sure that new
		   firmware will not be rejected because of older version. It is safe to always
		   pass firmware because setup in bootloader mode can only happen during
		   emergency update */
		if (broken_by_flag || version_raw == G_MAXUINT32) {
			version_raw = 0;
		}
	}

	return fu_corsair_version_from_uint32(version_raw);
}

static gchar *
fu_corsair_device_get_bootloader_version(FuCorsairBp *self, GError **error)
{
	guint32 version_raw;

	if (!fu_corsair_bp_get_property(self,
					FU_CORSAIR_BP_PROPERTY_BOOTLOADER_VERSION,
					&version_raw,
					error))
		return NULL;

	return fu_corsair_version_from_uint32(version_raw);
}

static gboolean
fu_corsair_device_setup(FuDevice *device, GError **error)
{
	guint32 mode;
	guint32 battery_level;
	g_autofree gchar *bootloader_version = NULL;
	g_autofree gchar *version = NULL;
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);

	if (!fu_corsair_bp_get_property(self->bp, FU_CORSAIR_BP_PROPERTY_MODE, &mode, error))
		return FALSE;
	if (mode == FU_CORSAIR_DEVICE_MODE_BOOTLOADER)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

	version = fu_corsair_device_get_version(device, error);
	if (version == NULL) {
		g_prefix_error(error, "cannot get version: ");
		return FALSE;
	}
	fu_device_set_version(device, version);

	bootloader_version = fu_corsair_device_get_bootloader_version(self->bp, error);
	if (bootloader_version == NULL) {
		g_prefix_error(error, "cannot get bootloader version: ");
		return FALSE;
	}
	fu_device_set_version_bootloader(device, bootloader_version);

	if (fu_device_has_private_flag(device, FU_CORSAIR_DEVICE_FLAG_IS_SUBDEVICE) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_corsair_bp_get_property(self->bp,
						FU_CORSAIR_BP_PROPERTY_BATTERY_LEVEL,
						&battery_level,
						error)) {
			g_prefix_error(error, "cannot get battery level: ");
			return FALSE;
		}
		fu_device_set_battery_level(device, battery_level / 10);
	}
	fu_corsair_bp_set_legacy_attach(
	    self->bp,
	    fu_device_has_private_flag(device, FU_CORSAIR_DEVICE_FLAG_LEGACY_ATTACH));

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* check for a subdevice */
	if (self->subdevice_id != NULL) {
		gboolean subdevice_added = FALSE;
		g_autoptr(GError) local_error = NULL;
		if (!fu_corsair_poll_subdevice(device, &subdevice_added, &local_error)) {
			g_warning("error polling subdevice: %s", local_error->message);
		} else {
			if (!subdevice_added)
				fu_device_set_poll_interval(device, CORSAIR_SUBDEVICE_POLL_PERIOD);
		}
	}

	return TRUE;
}

static gboolean
fu_corsair_device_reload(FuDevice *device, GError **error)
{
	if (fu_device_has_private_flag(device, FU_CORSAIR_DEVICE_FLAG_IS_SUBDEVICE)) {
		return fu_corsair_device_setup(device, error);
	}

	/* USB devices will be reloaded by FWUPD after reenumeration */
	return TRUE;
}

static gboolean
fu_corsair_is_subdevice_connected_cb(FuDevice *device, gpointer user_data, GError **error)
{
	guint32 subdevices = 0;
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);

	if (!fu_corsair_bp_get_property(self->bp,
					FU_CORSAIR_BP_PROPERTY_SUBDEVICES,
					&subdevices,
					error)) {
		g_prefix_error(error, "cannot get subdevices: ");
		return FALSE;
	}

	if (subdevices == 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "subdevice is not connected");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_corsair_reconnect_subdevice(FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);

	if (parent == NULL) {
		g_prefix_error(error, "cannot get parent: ");
		return FALSE;
	}

	/* Wait some time to make sure that a subdevice was disconnected. */
	g_usleep(CORSAIR_SUBDEVICE_REBOOT_DELAY);

	if (!fu_device_retry_full(parent,
				  fu_corsair_is_subdevice_connected_cb,
				  CORSAIR_SUBDEVICE_RECONNECT_RETRIES,
				  CORSAIR_SUBDEVICE_RECONNECT_PERIOD,
				  NULL,
				  error)) {
		g_prefix_error(error, "a subdevice did not reconnect after attach: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_corsair_ensure_mode(FuDevice *device, FuCorsairDeviceMode mode, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	FuCorsairDeviceMode current_mode;

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		current_mode = FU_CORSAIR_DEVICE_MODE_BOOTLOADER;
	} else {
		current_mode = FU_CORSAIR_DEVICE_MODE_APPLICATION;
	}

	if (mode == current_mode)
		return TRUE;

	if (mode == FU_CORSAIR_DEVICE_MODE_APPLICATION) {
		if (!fu_device_attach(FU_DEVICE(self->bp), error)) {
			g_prefix_error(error, "attach failed: ");
			return FALSE;
		}
	} else {
		if (!fu_device_detach(FU_DEVICE(self->bp), error)) {
			g_prefix_error(error, "detach failed: ");
			return FALSE;
		}
	}

	if (fu_device_has_private_flag(device, FU_CORSAIR_DEVICE_FLAG_IS_SUBDEVICE)) {
		if (!fu_corsair_reconnect_subdevice(device, error)) {
			g_prefix_error(error, "subdevice did not reconnect: ");
			return FALSE;
		}
		if (mode == FU_CORSAIR_DEVICE_MODE_BOOTLOADER) {
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		} else {
			fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		}
	} else {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	}

	return TRUE;
}

static gboolean
fu_corsair_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	return fu_corsair_ensure_mode(device, FU_CORSAIR_DEVICE_MODE_APPLICATION, error);
}

static gboolean
fu_corsair_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	return fu_corsair_ensure_mode(device, FU_CORSAIR_DEVICE_MODE_BOOTLOADER, error);
}

static gboolean
fu_corsair_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	g_autoptr(GBytes) firmware_bytes = fu_firmware_get_bytes(firmware, error);

	if (firmware_bytes == NULL) {
		g_prefix_error(error, "cannot get firmware data");
		return FALSE;
	}

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5);

	if (!fu_device_write_firmware(FU_DEVICE(self->bp),
				      firmware_bytes,
				      fu_progress_get_child(progress),
				      flags,
				      error)) {
		g_prefix_error(error, "cannot write firmware");
		return FALSE;
	}

	fu_progress_step_done(progress);

	if (!fu_device_has_private_flag(device, FU_CORSAIR_DEVICE_FLAG_LEGACY_ATTACH)) {
		if (!fu_corsair_bp_activate_firmware(self->bp, firmware, error)) {
			g_prefix_error(error, "firmware activation fail: ");
			return FALSE;
		}
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	}

	fu_progress_step_done(progress);

	return TRUE;
}

static void
fu_corsair_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);

	FU_DEVICE_CLASS(fu_corsair_device_parent_class)->to_string(device, idt, str);

	fu_common_string_append_kv(str,
				   idt,
				   "DeviceKind",
				   fu_corsair_device_type_to_string(self->device_kind));

	fu_device_add_string(FU_DEVICE(self->bp), idt, str);
}

static void
fu_corsair_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 92);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4); /* attach */
}

static gboolean
fu_corsair_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	guint64 vendor_interface;

	if (g_strcmp0(key, "CorsairDeviceKind") == 0) {
		self->device_kind = fu_corsair_device_type_from_string(value);
		if (self->device_kind != FU_CORSAIR_DEVICE_UNKNOWN)
			return TRUE;

		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "unsupported device in quirk");
		return FALSE;
	} else if (g_strcmp0(key, "CorsairVendorInterfaceId") == 0) {
		/* clapped to uint8 because bNumInterfaces is 8 bits long */
		if (!fu_common_strtoull_full(value, &vendor_interface, 0, 255, error)) {
			g_prefix_error(error, "cannot parse CorsairVendorInterface: ");
			return FALSE;
		}
		self->vendor_interface = vendor_interface;
		return TRUE;
	} else if (g_strcmp0(key, "CorsairSubdeviceId") == 0) {
		self->subdevice_id = g_strdup(value);
		return TRUE;
	}

	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
	return FALSE;
}

static gboolean
fu_corsair_device_poll(FuDevice *device, GError **error)
{
	gboolean subdevice_added = FALSE;
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(device, error);
	if (locker == NULL) {
		g_prefix_error(error, "cannot open device: ");
		return FALSE;
	}

	if (!fu_corsair_poll_subdevice(device, &subdevice_added, error)) {
		return FALSE;
	}

	/* stop polling if a subdevice was added */
	if (subdevice_added) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    "subdevice added successfully");
		return FALSE;
	}

	return TRUE;
}

static void
fu_corsair_device_finalize(GObject *object)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(object);

	g_free(self->subdevice_id);
	g_object_unref(self->bp);
}

static void
fu_corsair_device_class_init(FuCorsairDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	klass_device->poll = fu_corsair_device_poll;
	klass_device->probe = fu_corsair_device_probe;
	klass_device->set_quirk_kv = fu_corsair_set_quirk_kv;
	klass_device->setup = fu_corsair_device_setup;
	klass_device->reload = fu_corsair_device_reload;
	klass_device->attach = fu_corsair_device_attach;
	klass_device->detach = fu_corsair_device_detach;
	klass_device->write_firmware = fu_corsair_device_write_firmware;
	klass_device->to_string = fu_corsair_device_to_string;
	klass_device->set_progress = fu_corsair_device_set_progress;

	object_class->finalize = fu_corsair_device_finalize;
}

static void
fu_corsair_device_init(FuCorsairDevice *device)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);

	self->device_kind = FU_CORSAIR_DEVICE_MOUSE;
	self->vendor_interface = CORSAIR_DEFAULT_VENDOR_INTERFACE_ID;

	fu_device_register_private_flag(FU_DEVICE(device),
					FU_CORSAIR_DEVICE_FLAG_IS_SUBDEVICE,
					"is-subdevice");
	fu_device_register_private_flag(FU_DEVICE(device),
					FU_CORSAIR_DEVICE_FLAG_LEGACY_ATTACH,
					"legacy-attach");
	fu_device_register_private_flag(FU_DEVICE(device),
					FU_CORSAIR_DEVICE_FLAG_NO_VERSION_IN_BOOTLOADER,
					"no-version-in-bl");

	fu_device_set_remove_delay(FU_DEVICE(device), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(device), FWUPD_VERSION_FORMAT_TRIPLET);

	fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	fu_device_add_internal_flag(FU_DEVICE(device), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_protocol(FU_DEVICE(device), "com.corsair.bp");
}

FuCorsairDevice *
fu_corsair_device_new(FuCorsairDevice *parent, FuCorsairBp *bp)
{
	FuCorsairDevice *self = NULL;
	FuDevice *device = FU_DEVICE(parent);

	self = g_object_new(FU_TYPE_CORSAIR_DEVICE,
			    "context",
			    fu_device_get_context(device),
			    "usb_device",
			    fu_usb_device_get_dev(FU_USB_DEVICE(device)),
			    NULL);
	self->bp = g_object_ref(bp);
	return self;
}
