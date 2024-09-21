/*
 * Copyright 2022 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-corsair-bp.h"
#include "fu-corsair-common.h"
#include "fu-corsair-device.h"
#include "fu-corsair-struct.h"

#define CORSAIR_DEFAULT_VENDOR_INTERFACE_ID 1

#define CORSAIR_TRANSACTION_TIMEOUT	    4000
#define CORSAIR_SUBDEVICE_POLL_PERIOD	    30000
#define CORSAIR_SUBDEVICE_REBOOT_DELAY	    4000 /* ms */
#define CORSAIR_SUBDEVICE_RECONNECT_RETRIES 30
#define CORSAIR_SUBDEVICE_RECONNECT_PERIOD  1000
#define CORSAIR_SUBDEVICE_FIRST_POLL_DELAY  2000 /* ms */

#define FU_CORSAIR_DEVICE_FLAG_LEGACY_ATTACH		"legacy-attach"
#define FU_CORSAIR_DEVICE_FLAG_IS_SUBDEVICE		"is-subdevice"
#define FU_CORSAIR_DEVICE_FLAG_NO_VERSION_IN_BOOTLOADER "no-version-in-bl"

struct _FuCorsairDevice {
	FuUsbDevice parent_instance;
	FuCorsairDeviceKind device_kind;
	guint8 vendor_interface;
	gchar *subdevice_id;
	FuCorsairBp *bp;
};
G_DEFINE_TYPE(FuCorsairDevice, fu_corsair_device, FU_TYPE_USB_DEVICE)

static FuCorsairDevice *
fu_corsair_device_new(FuCorsairDevice *parent, FuCorsairBp *bp);

static gboolean
fu_corsair_device_probe(FuDevice *device, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	FuUsbInterface *iface = NULL;
	FuUsbEndpoint *ep1 = NULL;
	FuUsbEndpoint *ep2 = NULL;
	g_autoptr(GPtrArray) ifaces = NULL;
	g_autoptr(GPtrArray) endpoints = NULL;
	guint16 cmd_write_size;
	guint16 cmd_read_size;
	guint8 epin;
	guint8 epout;

	/* probing are skipped for subdevices */
	if (fu_device_has_private_flag(device, FU_CORSAIR_DEVICE_FLAG_IS_SUBDEVICE))
		return TRUE;

	if (!FU_DEVICE_CLASS(fu_corsair_device_parent_class)->probe(device, error))
		return FALSE;

	ifaces = fu_usb_device_get_interfaces(FU_USB_DEVICE(self), error);
	if (ifaces == NULL || (ifaces->len < (self->vendor_interface + 1u))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "update interface not found");
		return FALSE;
	}

	iface = g_ptr_array_index(ifaces, self->vendor_interface);
	endpoints = fu_usb_interface_get_endpoints(iface);
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
	if (fu_usb_endpoint_get_direction(ep1) == FU_USB_DIRECTION_DEVICE_TO_HOST) {
		epin = fu_usb_endpoint_get_address(ep1);
		epout = fu_usb_endpoint_get_address(ep2);
		cmd_read_size = fu_usb_endpoint_get_maximum_packet_size(ep1);
		cmd_write_size = fu_usb_endpoint_get_maximum_packet_size(ep2);
	} else {
		epin = fu_usb_endpoint_get_address(ep2);
		epout = fu_usb_endpoint_get_address(ep1);
		cmd_read_size = fu_usb_endpoint_get_maximum_packet_size(ep2);
		cmd_write_size = fu_usb_endpoint_get_maximum_packet_size(ep1);
	}

	if (cmd_write_size > FU_CORSAIR_MAX_CMD_SIZE || cmd_read_size > FU_CORSAIR_MAX_CMD_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "endpoint size is bigger than allowed command size");
		return FALSE;
	}

	fu_usb_device_add_interface(FU_USB_DEVICE(self), self->vendor_interface);

	self->bp = fu_corsair_bp_new(FU_USB_DEVICE(device), FALSE);
	fu_corsair_bp_set_cmd_size(self->bp, cmd_write_size, cmd_read_size);
	fu_corsair_bp_set_endpoints(self->bp, epin, epout);

	return TRUE;
}

static gboolean
fu_corsair_device_poll_subdevice(FuDevice *device, gboolean *subdevice_added, GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	guint32 subdevices;
	g_autoptr(FuCorsairDevice) child = NULL;
	g_autoptr(FuCorsairBp) child_bp = NULL;

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

	child_bp = fu_corsair_bp_new(FU_USB_DEVICE(device), TRUE);
	fu_device_incorporate(FU_DEVICE(child_bp),
			      FU_DEVICE(self->bp),
			      FU_DEVICE_INCORPORATE_FLAG_ALL);

	child = fu_corsair_device_new(self, child_bp);
	fu_device_add_instance_id(FU_DEVICE(child), self->subdevice_id);
	fu_device_set_logical_id(FU_DEVICE(child), "subdevice");
	fu_device_add_private_flag(FU_DEVICE(child), FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN);

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

	fu_corsair_bp_flush_input_reports(self->bp);

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
	if (self->subdevice_id != NULL &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		gboolean subdevice_added = FALSE;
		g_autoptr(GError) local_error = NULL;

		/* Give some time to a subdevice to get connected to the receiver.
		 * Without this delay a subdevice may be not present even if it is
		 * turned on. */
		fu_device_sleep(device, CORSAIR_SUBDEVICE_FIRST_POLL_DELAY);
		if (!fu_corsair_device_poll_subdevice(device, &subdevice_added, &local_error)) {
			g_warning("error polling subdevice: %s", local_error->message);
		} else {
			/* start polling if a subdevice was not added */
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
fu_corsair_device_is_subdevice_connected_cb(FuDevice *device, gpointer user_data, GError **error)
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
fu_corsair_device_reconnect_subdevice(FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);

	if (parent == NULL) {
		g_prefix_error(error, "cannot get parent: ");
		return FALSE;
	}

	/* Wait some time to make sure that a subdevice was disconnected. */
	fu_device_sleep(device, CORSAIR_SUBDEVICE_REBOOT_DELAY);

	if (!fu_device_retry_full(parent,
				  fu_corsair_device_is_subdevice_connected_cb,
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
fu_corsair_device_ensure_mode(FuDevice *device, FuCorsairDeviceMode mode, GError **error)
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
		if (!fu_corsair_device_reconnect_subdevice(device, error)) {
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
	return fu_corsair_device_ensure_mode(device, FU_CORSAIR_DEVICE_MODE_APPLICATION, error);
}

static gboolean
fu_corsair_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	return fu_corsair_device_ensure_mode(device, FU_CORSAIR_DEVICE_MODE_BOOTLOADER, error);
}

static gboolean
fu_corsair_device_write_firmware(FuDevice *device,
				 FuFirmware *firmware,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	g_autoptr(GInputStream) stream = fu_firmware_get_stream(firmware, error);

	if (stream == NULL) {
		g_prefix_error(error, "cannot get firmware stream: ");
		return FALSE;
	}

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, NULL);

	if (!fu_device_write_firmware(FU_DEVICE(self->bp),
				      stream,
				      fu_progress_get_child(progress),
				      flags,
				      error)) {
		g_prefix_error(error, "cannot write firmware: ");
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
	fwupd_codec_string_append(str,
				  idt,
				  "DeviceKind",
				  fu_corsair_device_kind_to_string(self->device_kind));

	fu_device_add_string(FU_DEVICE(self->bp), idt, str);
}

static void
fu_corsair_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 92, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 4, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static gboolean
fu_corsair_device_set_quirk_kv(FuDevice *device,
			       const gchar *key,
			       const gchar *value,
			       GError **error)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);
	guint64 vendor_interface;

	if (g_strcmp0(key, "CorsairDeviceKind") == 0) {
		self->device_kind = fu_corsair_device_kind_from_string(value);
		if (self->device_kind != FU_CORSAIR_DEVICE_KIND_UNKNOWN)
			return TRUE;

		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "unsupported device in quirk");
		return FALSE;
	}
	if (g_strcmp0(key, "CorsairVendorInterfaceId") == 0) {
		/* clapped to uint8 because bNumInterfaces is 8 bits long */
		if (!fu_strtoull(value,
				 &vendor_interface,
				 0,
				 G_MAXUINT8,
				 FU_INTEGER_BASE_AUTO,
				 error)) {
			g_prefix_error(error, "cannot parse CorsairVendorInterface: ");
			return FALSE;
		}
		self->vendor_interface = vendor_interface;
		return TRUE;
	}
	if (g_strcmp0(key, "CorsairSubdeviceId") == 0) {
		self->subdevice_id = g_strdup(value);
		return TRUE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
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

	if (!fu_corsair_device_poll_subdevice(device, &subdevice_added, error)) {
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

	G_OBJECT_CLASS(fu_corsair_device_parent_class)->finalize(object);
}

static void
fu_corsair_device_class_init(FuCorsairDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	device_class->poll = fu_corsair_device_poll;
	device_class->probe = fu_corsair_device_probe;
	device_class->set_quirk_kv = fu_corsair_device_set_quirk_kv;
	device_class->setup = fu_corsair_device_setup;
	device_class->reload = fu_corsair_device_reload;
	device_class->attach = fu_corsair_device_attach;
	device_class->detach = fu_corsair_device_detach;
	device_class->write_firmware = fu_corsair_device_write_firmware;
	device_class->to_string = fu_corsair_device_to_string;
	device_class->set_progress = fu_corsair_device_set_progress;

	object_class->finalize = fu_corsair_device_finalize;
}

static void
fu_corsair_device_init(FuCorsairDevice *device)
{
	FuCorsairDevice *self = FU_CORSAIR_DEVICE(device);

	self->device_kind = FU_CORSAIR_DEVICE_KIND_MOUSE;
	self->vendor_interface = CORSAIR_DEFAULT_VENDOR_INTERFACE_ID;

	fu_device_register_private_flag(FU_DEVICE(device), FU_CORSAIR_DEVICE_FLAG_IS_SUBDEVICE);
	fu_device_register_private_flag(FU_DEVICE(device), FU_CORSAIR_DEVICE_FLAG_LEGACY_ATTACH);
	fu_device_register_private_flag(FU_DEVICE(device),
					FU_CORSAIR_DEVICE_FLAG_NO_VERSION_IN_BOOTLOADER);

	fu_device_set_remove_delay(FU_DEVICE(device), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(device), FWUPD_VERSION_FORMAT_TRIPLET);

	fu_device_add_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	fu_device_add_private_flag(FU_DEVICE(device), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_protocol(FU_DEVICE(device), "com.corsair.bp");
}

static FuCorsairDevice *
fu_corsair_device_new(FuCorsairDevice *parent, FuCorsairBp *bp)
{
	FuCorsairDevice *self = NULL;

	self = g_object_new(FU_TYPE_CORSAIR_DEVICE,
			    "context",
			    fu_device_get_context(FU_DEVICE(parent)),
			    NULL);
	fu_device_incorporate(FU_DEVICE(self), FU_DEVICE(parent), FU_DEVICE_INCORPORATE_FLAG_ALL);
	self->bp = g_object_ref(bp);
	return self;
}
