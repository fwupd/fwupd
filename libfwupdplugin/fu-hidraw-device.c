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

static gboolean
fu_hidraw_device_probe(FuDevice *device, GError **error)
{
	FuHidrawDevice *self = FU_HIDRAW_DEVICE(device);
	g_autofree gchar *prop_id = NULL;
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
 * @flags: some #FuUdevDeviceIoctlFlags, e.g. %FU_UDEV_DEVICE_IOCTL_FLAG_RETRY
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
			     FuUdevDeviceIoctlFlags flags,
			     GError **error)
{
#ifdef HAVE_HIDRAW_H
	g_autofree guint8 *buf_mut = NULL;
#endif

	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

#ifdef HAVE_HIDRAW_H
	fu_dump_raw(G_LOG_DOMAIN, "SetFeature", buf, bufsz);
	buf_mut = fu_memdup_safe(buf, bufsz, error);
	if (buf_mut == NULL)
		return FALSE;
	return fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
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
 * @flags: some #FuUdevDeviceIoctlFlags, e.g. %FU_UDEV_DEVICE_IOCTL_FLAG_RETRY
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
			     FuUdevDeviceIoctlFlags flags,
			     GError **error)
{
	g_return_val_if_fail(FU_IS_HIDRAW_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

#ifdef HAVE_HIDRAW_H
	fu_dump_raw(G_LOG_DOMAIN, "GetFeature[req]", buf, bufsz);
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
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

static void
fu_hidraw_device_init(FuHidrawDevice *self)
{
}

static void
fu_hidraw_device_class_init(FuHidrawDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_hidraw_device_probe;
}
