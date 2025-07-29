/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuHidrawDevice"

#include "config.h"

#ifdef HAVE_HIDRAW_H
#include <linux/hidraw.h>
#include <linux/input.h>
#endif

#include "fu-dump.h"
#include "fu-hidraw-device.h"
#include "fu-mem.h"
#include "fu-string.h"
#include "fu-udev-device-private.h"

/**
 * FuHidrawDevice
 *
 * See also: #FuUdevDevice
 */

G_DEFINE_TYPE(FuHidrawDevice, fu_hidraw_device, FU_TYPE_UDEV_DEVICE)

#define FU_HIDRAW_DEVICE_IOCTL_TIMEOUT 2500 /* ms */

/**
 * fu_hidraw_device_parse_descriptor:
 * @self: a #FuHidrawDevice
 * @error: (nullable): optional return location for an error
 *
 * Retrieves and parses the HID descriptor.
 *
 * Returns: (transfer full): a #FuHidDescriptor, or %NULL on error
 *
 * Since: 2.0.12
 **/
FuHidDescriptor *
fu_hidraw_device_parse_descriptor(FuHidrawDevice *self, GError **error)
{
#ifdef HAVE_HIDRAW_H
	gint desc_size = 0;
	struct hidraw_report_descriptor rpt_desc = {0x0};
	g_autoptr(FuFirmware) descriptor = fu_hid_descriptor_new();
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));
	g_autoptr(GBytes) fw = NULL;

	/* Get Report Descriptor Size */
	if (!fu_ioctl_execute(ioctl,
			      HIDIOCGRDESCSIZE,
			      (guint8 *)&desc_size,
			      sizeof(desc_size),
			      NULL,
			      5000,
			      FU_IOCTL_FLAG_NONE,
			      error)) {
		g_prefix_error(error, "failed to get report descriptor size: ");
		return NULL;
	}

	rpt_desc.size = desc_size;
	if (!fu_ioctl_execute(ioctl,
			      HIDIOCGRDESC,
			      (guint8 *)&rpt_desc,
			      sizeof(rpt_desc),
			      NULL,
			      5000,
			      FU_IOCTL_FLAG_NONE,
			      error)) {
		g_prefix_error(error, "failed to get report descriptor: ");
		return NULL;
	}
	fu_dump_raw(G_LOG_DOMAIN, "HID descriptor", rpt_desc.value, rpt_desc.size);

	fw = g_bytes_new(rpt_desc.value, rpt_desc.size);
	if (!fu_firmware_parse_bytes(descriptor, fw, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, error))
		return NULL;
	return FU_HID_DESCRIPTOR(g_steal_pointer(&descriptor));
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return NULL;
#endif /* HAVE_HIDRAW_H */
}

static gboolean
fu_hidraw_device_probe_usb(FuHidrawDevice *self, GError **error)
{
	g_autoptr(FuDevice) usb_device = NULL;

	usb_device =
	    fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), "usb:usb_device", error);
	if (usb_device == NULL)
		return FALSE;
	fu_device_incorporate(FU_DEVICE(self),
			      FU_DEVICE(usb_device),
			      FU_DEVICE_INCORPORATE_FLAG_POSSIBLE_PLUGINS |
				  FU_DEVICE_INCORPORATE_FLAG_GTYPE);

	/* success */
	return TRUE;
}

static gboolean
fu_hidraw_device_probe(FuDevice *device, GError **error)
{
	FuHidrawDevice *self = FU_HIDRAW_DEVICE(device);
	g_autofree gchar *prop_id = NULL;
	g_autofree gchar *version = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(FuDevice) hid_device = NULL;

	/* get device */
	if (!fu_udev_device_parse_number(FU_UDEV_DEVICE(self), error))
		return FALSE;

	/* get parent */
	hid_device = fu_device_get_backend_parent_with_subsystem(device, "hid", error);
	if (hid_device == NULL)
		return FALSE;

	/* ID */
	prop_id = fu_udev_device_read_property(FU_UDEV_DEVICE(hid_device), "HID_ID", error);
	if (prop_id == NULL)
		return FALSE;
	split = g_strsplit(prop_id, ":", -1);
	if (g_strv_length(split) == 3) {
		if (fu_device_get_vendor(FU_DEVICE(self)) == NULL) {
			guint64 val = 0;
			if (!fu_strtoull(split[1],
					 &val,
					 0,
					 G_MAXUINT16,
					 FU_INTEGER_BASE_16,
					 error)) {
				g_prefix_error(error, "failed to parse HID_ID: ");
				return FALSE;
			}
			fu_device_set_vid(device, (guint16)val);
		}
		if (fu_device_get_pid(device) == 0x0) {
			guint64 val = 0;
			if (!fu_strtoull(split[2],
					 &val,
					 0,
					 G_MAXUINT16,
					 FU_INTEGER_BASE_16,
					 error)) {
				g_prefix_error(error, "failed to parse HID_ID: ");
				return FALSE;
			}
			fu_device_set_pid(device, (guint16)val);
		}
	}

	/* set name */
	if (fu_device_get_name(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *prop_name =
		    fu_udev_device_read_property(FU_UDEV_DEVICE(hid_device), "HID_NAME", NULL);
		if (prop_name != NULL)
			fu_device_set_name(FU_DEVICE(self), prop_name);
	}

	/* set the logical ID */
	if (fu_device_get_logical_id(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *logical_id =
		    fu_udev_device_read_property(FU_UDEV_DEVICE(hid_device), "HID_UNIQ", NULL);
		if (logical_id != NULL && logical_id[0] != '\0')
			fu_device_set_logical_id(FU_DEVICE(self), logical_id);
	}

	/* set the physical ID */
	if (fu_device_get_physical_id(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *physical_id = NULL;
		physical_id =
		    fu_udev_device_read_property(FU_UDEV_DEVICE(hid_device), "HID_PHYS", error);
		if (physical_id == NULL)
			return FALSE;
		fu_device_set_physical_id(FU_DEVICE(self), physical_id);

		/* this is from a USB device, so try to use the DS-20 descriptor */
		if (g_str_has_prefix(physical_id, "usb")) {
			if (!fu_hidraw_device_probe_usb(self, error))
				return FALSE;
		}
	}

	version =
	    fu_udev_device_read_property(FU_UDEV_DEVICE(hid_device), "HID_FIRMWARE_VERSION", NULL);
	if (version != NULL) {
		guint64 hid_version = 0;
		g_autoptr(GError) error_local = NULL;

		if (!fu_strtoull(version,
				 &hid_version,
				 0x0,
				 G_MAXUINT64,
				 FU_INTEGER_BASE_AUTO,
				 &error_local)) {
			g_info("failed to parse HID_FIRMWARE_VERSION: %s", error_local->message);
		} else
			fu_device_set_version_raw(FU_DEVICE(self), hid_version);
	}

	/* set the hidraw device */
	if (fu_udev_device_get_device_file(FU_UDEV_DEVICE(self)) == NULL) {
		g_autofree gchar *device_file = NULL;
		device_file =
		    fu_udev_device_get_device_file_from_subsystem(FU_UDEV_DEVICE(hid_device),
								  "hidraw",
								  error);
		if (device_file == NULL)
			return FALSE;
		fu_udev_device_set_device_file(FU_UDEV_DEVICE(self), device_file);
	}

	/* USB\\VID_1234 */
	fu_device_add_instance_u16(FU_DEVICE(self), "VEN", fu_device_get_vid(device));
	fu_device_add_instance_u16(FU_DEVICE(self), "DEV", fu_device_get_pid(device));
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_GENERIC |
					     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "HIDRAW",
					 "VEN",
					 NULL);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_GENERIC |
					     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
					     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "HIDRAW",
					 "VEN",
					 "DEV",
					 NULL);
	fu_device_build_vendor_id_u16(device, "HIDRAW", fu_device_get_vid(device));

	/* success */
	return TRUE;
}

/**
 * fu_hidraw_device_set_feature:
 * @self: a #FuHidrawDevice
 * @buf: (not nullable): a buffer to use, which *must* be large enough for the request
 * @bufsz: the size of @buf
 * @flags: some #FuIoctlFlags, e.g. %FU_IOCTL_FLAG_RETRY
 * @error: (nullable): optional return location for an error
 *
 * Do a HID SetFeature request.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_hidraw_device_set_feature(FuHidrawDevice *self,
			     const guint8 *buf,
			     gsize bufsz,
			     FuIoctlFlags flags,
			     GError **error)
{
#ifdef HAVE_HIDRAW_H
	g_autofree guint8 *buf_mut = NULL;
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));
#endif

	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

#ifdef HAVE_HIDRAW_H
	fu_dump_raw(G_LOG_DOMAIN, "SetFeature", buf, bufsz);
	buf_mut = fu_memdup_safe(buf, bufsz, error);
	if (buf_mut == NULL)
		return FALSE;
	return fu_ioctl_execute(ioctl,
				HIDIOCSFEATURE(bufsz), /* nocheck:blocked */
				buf_mut,
				bufsz,
				NULL,
				FU_HIDRAW_DEVICE_IOCTL_TIMEOUT,
				flags,
				error);
#else
	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

/**
 * fu_hidraw_device_get_feature:
 * @self: a #FuHidrawDevice
 * @buf: (not nullable): a buffer to use, which *must* be large enough for the request
 * @bufsz: the size of @buf
 * @flags: some #FuIoctlFlags, e.g. %FU_IOCTL_FLAG_RETRY
 * @error: (nullable): optional return location for an error
 *
 * Do a HID GetFeature request.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_hidraw_device_get_feature(FuHidrawDevice *self,
			     guint8 *buf,
			     gsize bufsz,
			     FuIoctlFlags flags,
			     GError **error)
{
#ifdef HAVE_HIDRAW_H
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));
#endif

	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

#ifdef HAVE_HIDRAW_H
	fu_dump_raw(G_LOG_DOMAIN, "GetFeature[req]", buf, bufsz);
	if (!fu_ioctl_execute(ioctl,
			      HIDIOCGFEATURE(bufsz), /* nocheck:blocked */
			      buf,
			      bufsz,
			      NULL,
			      FU_HIDRAW_DEVICE_IOCTL_TIMEOUT,
			      flags,
			      error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "GetFeature[res]", buf, bufsz);

	/* success */
	return TRUE;
#else
	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

/**
 * fu_hidraw_device_set_report:
 * @self: a #FuHidrawDevice
 * @buf: (not nullable): a buffer to use, which *must* be large enough for the request
 * @bufsz: the size of @buf
 * @flags: some #FuIOChannelFlags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: (nullable): optional return location for an error
 *
 * Do a HID SetOutputReport request.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.14
 **/
gboolean
fu_hidraw_device_set_report(FuHidrawDevice *self,
			    const guint8 *buf,
			    gsize bufsz,
			    FuIOChannelFlags flags,
			    GError **error)
{
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	fu_dump_raw(G_LOG_DOMAIN, "SetReport", buf, bufsz);
	return fu_udev_device_write(FU_UDEV_DEVICE(self),
				    buf,
				    bufsz,
				    FU_HIDRAW_DEVICE_IOCTL_TIMEOUT,
				    flags,
				    error);
}

/**
 * fu_hidraw_device_get_report:
 * @self: a #FuHidrawDevice
 * @buf: (not nullable): a buffer to use, which *must* be large enough for the request
 * @bufsz: the size of @buf
 * @flags: some #FuIOChannelFlags, e.g. %FU_IO_CHANNEL_FLAG_SINGLE_SHOT
 * @error: (nullable): optional return location for an error
 *
 * Do a HID GetInputReport request.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.14
 **/
gboolean
fu_hidraw_device_get_report(FuHidrawDevice *self,
			    guint8 *buf,
			    gsize bufsz,
			    FuIOChannelFlags flags,
			    GError **error)
{
	gsize bytes_read = 0;

	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	fu_dump_raw(G_LOG_DOMAIN, "GetReport", buf, bufsz);
	if (!fu_udev_device_read(FU_UDEV_DEVICE(self),
				 buf,
				 bufsz,
				 &bytes_read,
				 FU_HIDRAW_DEVICE_IOCTL_TIMEOUT,
				 flags,
				 error))
		return FALSE;

	if (bytes_read != bufsz) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "invalid response");
		return FALSE;
	}
	return TRUE;
}

static void
fu_hidraw_device_init(FuHidrawDevice *self)
{
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
}

static void
fu_hidraw_device_class_init(FuHidrawDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_hidraw_device_probe;
}
