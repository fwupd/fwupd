/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuUdevDevice"

#include "config.h"

#include <fcntl.h>
#include <string.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_IOCTL_H
#include <sys/ioctl.h>
#endif
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fu-device-event-private.h"
#include "fu-device-private.h"
#include "fu-i2c-device.h"
#include "fu-string.h"
#include "fu-udev-device-private.h"
#include "fu-usb-device-private.h"

/**
 * FuUdevDevice:
 *
 * A UDev device, typically only available on Linux.
 *
 * See also: [class@FuDevice]
 */

typedef struct {
	GUdevDevice *udev_device;
	gboolean udev_device_cleared;
	guint32 class;
	guint16 vendor;
	guint16 model;
	guint16 subsystem_vendor;
	guint16 subsystem_model;
	guint8 revision;
	gchar *subsystem;
	gchar *bind_id;
	gchar *driver;
	gchar *device_file;
	gchar *devtype;
	guint64 number;
	FuIOChannel *io_channel;
	FuUdevDeviceFlags flags;
} FuUdevDevicePrivate;

static void
fu_udev_device_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuUdevDevice,
		       fu_udev_device,
		       FU_TYPE_DEVICE,
		       0,
		       G_ADD_PRIVATE(FuUdevDevice)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC,
						 fu_udev_device_codec_iface_init));

enum {
	PROP_0,
	PROP_UDEV_DEVICE,
	PROP_SUBSYSTEM,
	PROP_DRIVER,
	PROP_DEVICE_FILE,
	PROP_BIND_ID,
	PROP_DEVTYPE,
	PROP_LAST
};

enum { SIGNAL_CHANGED, SIGNAL_LAST };

static guint signals[SIGNAL_LAST] = {0};

#define GET_PRIVATE(o) (fu_udev_device_get_instance_private(o))

/**
 * fu_udev_device_emit_changed:
 * @self: a #FuUdevDevice
 *
 * Emits the ::changed signal for the object.
 *
 * Since: 1.1.2
 **/
void
fu_udev_device_emit_changed(FuUdevDevice *self)
{
	g_autoptr(GError) error = NULL;
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	g_debug("FuUdevDevice emit changed");
	if (!fu_device_rescan(FU_DEVICE(self), &error))
		g_debug("%s", error->message);
	g_signal_emit(self, signals[SIGNAL_CHANGED], 0);
}

#ifdef HAVE_GUDEV
static guint32
fu_udev_device_get_sysfs_attr_as_uint32(GUdevDevice *udev_device, const gchar *name)
{
	const gchar *tmp;
	guint64 tmp64 = 0;
	g_autoptr(GError) error_local = NULL;

	tmp = g_udev_device_get_sysfs_attr(udev_device, name);
	if (tmp == NULL)
		return 0x0;
	if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, &error_local)) {
		g_warning("reading %s for %s was invalid: %s", name, tmp, error_local->message);
		return 0x0;
	}
	return tmp64;
}

static guint16
fu_udev_device_get_sysfs_attr_as_uint16(GUdevDevice *udev_device, const gchar *name)
{
	const gchar *tmp;
	guint64 tmp64 = 0;
	g_autoptr(GError) error_local = NULL;

	tmp = g_udev_device_get_sysfs_attr(udev_device, name);
	if (tmp == NULL)
		return 0x0;
	if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, &error_local)) {
		g_warning("reading %s for %s was invalid: %s", name, tmp, error_local->message);
		return 0x0;
	}
	return tmp64;
}

static guint8
fu_udev_device_get_sysfs_attr_as_uint8(GUdevDevice *udev_device, const gchar *name)
{
	const gchar *tmp;
	guint64 tmp64 = 0;
	g_autoptr(GError) error_local = NULL;

	tmp = g_udev_device_get_sysfs_attr(udev_device, name);
	if (tmp == NULL)
		return 0x0;
	if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, &error_local)) {
		g_warning("reading %s for %s was invalid: %s",
			  name,
			  g_udev_device_get_sysfs_path(udev_device),
			  error_local->message);
		return 0x0;
	}
	return tmp64;
}

#endif

static void
fu_udev_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append_hex(str, idt, "Vendor", priv->vendor);
	fwupd_codec_string_append_hex(str, idt, "Model", priv->model);
	fwupd_codec_string_append_hex(str, idt, "SubsystemVendor", priv->subsystem_vendor);
	fwupd_codec_string_append_hex(str, idt, "SubsystemModel", priv->subsystem_model);
	fwupd_codec_string_append_hex(str, idt, "Class", priv->class);
	fwupd_codec_string_append_hex(str, idt, "Revision", priv->revision);
	fwupd_codec_string_append(str, idt, "Subsystem", priv->subsystem);
	fwupd_codec_string_append(str, idt, "Driver", priv->driver);
	fwupd_codec_string_append(str, idt, "BindId", priv->bind_id);
	fwupd_codec_string_append(str, idt, "DeviceFile", priv->device_file);
}

static gboolean
fu_udev_device_ensure_bind_id(FuUdevDevice *self, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* sanity check */
	if (priv->bind_id != NULL)
		return TRUE;

#ifdef HAVE_GUDEV
	/* automatically set the bind ID from the subsystem */
	if (g_strcmp0(priv->subsystem, "pci") == 0) {
		priv->bind_id =
		    g_strdup(g_udev_device_get_property(priv->udev_device, "PCI_SLOT_NAME"));
		return TRUE;
	}
	if (g_strcmp0(priv->subsystem, "hid") == 0) {
		priv->bind_id = g_strdup(g_udev_device_get_property(priv->udev_device, "HID_PHYS"));
		return TRUE;
	}
	if (g_strcmp0(priv->subsystem, "usb") == 0) {
		priv->bind_id =
		    g_path_get_basename(g_udev_device_get_sysfs_path(priv->udev_device));
		return TRUE;
	}
#endif

	/* nothing found automatically */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "cannot derive bind-id from subsystem %s",
		    priv->subsystem);
	return FALSE;
}

static void
fu_udev_device_set_subsystem(FuUdevDevice *self, const gchar *subsystem)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->subsystem, subsystem) == 0)
		return;

	g_free(priv->subsystem);
	priv->subsystem = g_strdup(subsystem);
	g_object_notify(G_OBJECT(self), "subsystem");
}

/**
 * fu_udev_device_set_bind_id:
 * @self: a #FuUdevDevice
 * @bind_id: a bind-id string, e.g. `pci:0:0:1`
 *
 * Sets the device ID used for binding the device, e.g. `pci:1:2:3`
 *
 * Since: 1.7.2
 **/
void
fu_udev_device_set_bind_id(FuUdevDevice *self, const gchar *bind_id)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->bind_id, bind_id) == 0)
		return;

	g_free(priv->bind_id);
	priv->bind_id = g_strdup(bind_id);
	g_object_notify(G_OBJECT(self), "bind-id");
}

static void
fu_udev_device_set_driver(FuUdevDevice *self, const gchar *driver)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->driver, driver) == 0)
		return;

	g_free(priv->driver);
	priv->driver = g_strdup(driver);
	g_object_notify(G_OBJECT(self), "driver");
}

/**
 * fu_udev_device_set_device_file:
 * @self: a #FuUdevDevice
 * @device_file: (nullable): a device path
 *
 * Sets the device file to use for reading and writing.
 *
 * Since: 1.8.7
 **/
void
fu_udev_device_set_device_file(FuUdevDevice *self, const gchar *device_file)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->device_file, device_file) == 0)
		return;

	g_free(priv->device_file);
	priv->device_file = g_strdup(device_file);
	g_object_notify(G_OBJECT(self), "device-file");
}

#ifdef HAVE_GUDEV
static const gchar *
fu_udev_device_get_vendor_fallback(GUdevDevice *udev_device)
{
	const gchar *tmp;
	tmp = g_udev_device_get_property(udev_device, "ID_VENDOR_FROM_DATABASE");
	if (tmp != NULL)
		return tmp;
	tmp = g_udev_device_get_property(udev_device, "ID_VENDOR");
	if (tmp != NULL)
		return tmp;
	return NULL;
}
#endif

#ifdef HAVE_GUDEV
static gboolean
fu_udev_device_probe_serio(FuUdevDevice *self, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;

	/* firmware ID */
	tmp = g_udev_device_get_property(priv->udev_device, "SERIO_FIRMWARE_ID");
	if (tmp != NULL) {
		/* this prefix is not useful */
		if (g_str_has_prefix(tmp, "PNP: "))
			tmp += 5;
		fu_device_add_instance_strsafe(FU_DEVICE(self), "FWID", tmp);
		if (!fu_device_build_instance_id_full(FU_DEVICE(self),
						      FU_DEVICE_INSTANCE_FLAG_GENERIC |
							  FU_DEVICE_INSTANCE_FLAG_VISIBLE |
							  FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						      error,
						      "SERIO",
						      "FWID",
						      NULL))
			return FALSE;
	}
	return TRUE;
}

static guint16
fu_udev_device_get_property_as_uint16(GUdevDevice *udev_device, const gchar *key)
{
	const gchar *tmp = g_udev_device_get_property(udev_device, key);
	guint64 value = 0;
	g_autofree gchar *str = NULL;

	if (tmp == NULL)
		return 0x0;
	str = g_strdup_printf("0x%s", tmp);
	if (!fu_strtoull(str, &value, 0x0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, NULL))
		return 0x0;
	return (guint16)value;
}

static void
fu_udev_device_set_vendor_from_udev_device(FuUdevDevice *self, GUdevDevice *udev_device)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	priv->vendor = fu_udev_device_get_sysfs_attr_as_uint16(udev_device, "vendor");
	priv->model = fu_udev_device_get_sysfs_attr_as_uint16(udev_device, "device");
	priv->revision = fu_udev_device_get_sysfs_attr_as_uint8(udev_device, "revision");
	priv->class = fu_udev_device_get_sysfs_attr_as_uint32(udev_device, "class");
	priv->subsystem_vendor =
	    fu_udev_device_get_sysfs_attr_as_uint16(udev_device, "subsystem_vendor");
	priv->subsystem_model =
	    fu_udev_device_get_sysfs_attr_as_uint16(udev_device, "subsystem_device");

	/* fallback to properties as udev might be using a subsystem-specific prober */
	if (priv->vendor == 0x0)
		priv->vendor = fu_udev_device_get_property_as_uint16(udev_device, "ID_VENDOR_ID");
	if (priv->model == 0x0)
		priv->model = fu_udev_device_get_property_as_uint16(udev_device, "ID_MODEL_ID");
	if (priv->revision == 0x0)
		priv->revision = fu_udev_device_get_property_as_uint16(udev_device, "ID_REVISION");
}

static void
fu_udev_device_set_vendor_from_parent(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GUdevDevice) udev_device = g_object_ref(priv->udev_device);
	while (TRUE) {
		g_autoptr(GUdevDevice) parent = g_udev_device_get_parent(udev_device);
		if (parent == NULL)
			break;
		fu_udev_device_set_vendor_from_udev_device(self, parent);
		if (priv->vendor != 0x0 || priv->model != 0x0 || priv->revision != 0x0)
			break;
		g_set_object(&udev_device, parent);
	}
}
#endif

static void
fu_udev_device_set_vendor(FuUdevDevice *self, guint16 vendor)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	priv->vendor = vendor;
}

static void
fu_udev_device_set_model(FuUdevDevice *self, guint16 model)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	priv->model = model;
}

static void
fu_udev_device_set_subsystem_vendor(FuUdevDevice *self, guint16 subsystem_vendor)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	priv->subsystem_vendor = subsystem_vendor;
}

static void
fu_udev_device_set_subsystem_model(FuUdevDevice *self, guint16 subsystem_model)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	priv->subsystem_model = subsystem_model;
}

static void
fu_udev_device_set_cls(FuUdevDevice *self, guint32 class)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	priv->class = class;
}

static void
fu_udev_device_set_revision(FuUdevDevice *self, guint8 revision)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	priv->revision = revision;
}

static void
fu_udev_device_set_number(FuUdevDevice *self, guint64 number)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	priv->number = number;
}

static void
fu_udev_device_set_devtype(FuUdevDevice *self, const gchar *devtype)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->devtype, devtype) == 0)
		return;

	g_free(priv->devtype);
	priv->devtype = g_strdup(devtype);
	g_object_notify(G_OBJECT(self), "devtype");
}

static gboolean
fu_udev_device_probe(FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
#ifdef HAVE_GUDEV
	const gchar *tmp;
	g_autofree gchar *subsystem = NULL;
	g_autoptr(GUdevDevice) udev_parent = NULL;
	g_autoptr(GUdevDevice) parent_i2c = NULL;
#endif

	/* nothing to do */
	if (priv->udev_device == NULL)
		return TRUE;

#ifdef HAVE_GUDEV
	/* get IDs, but fallback to the parent, grandparent, great-grandparent, etc */
	fu_udev_device_set_vendor_from_udev_device(self, priv->udev_device);
	udev_parent = g_udev_device_get_parent(priv->udev_device);
	if (udev_parent != NULL && priv->flags & FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT)
		fu_udev_device_set_vendor_from_parent(self);

	/* hidraw helpfully encodes the information in a different place */
	if (udev_parent != NULL && priv->vendor == 0x0 && priv->model == 0x0 &&
	    priv->revision == 0x0 && g_strcmp0(priv->subsystem, "hidraw") == 0) {
		tmp = g_udev_device_get_property(udev_parent, "HID_ID");
		if (tmp != NULL) {
			g_auto(GStrv) split = g_strsplit(tmp, ":", -1);
			if (g_strv_length(split) == 3) {
				guint64 val = 0;
				g_autoptr(GError) error_local = NULL;

				if (!fu_strtoull(split[1],
						 &val,
						 0,
						 G_MAXUINT16,
						 FU_INTEGER_BASE_16,
						 &error_local)) {
					g_warning("reading %s for %s failed: %s",
						  split[1],
						  g_udev_device_get_sysfs_path(priv->udev_device),
						  error_local->message);
				} else {
					priv->vendor = val;
				}
				if (!fu_strtoull(split[2],
						 &val,
						 0,
						 G_MAXUINT16,
						 FU_INTEGER_BASE_16,
						 &error_local)) {
					g_warning("reading %s for %s failed: %s",
						  split[2],
						  g_udev_device_get_sysfs_path(priv->udev_device),
						  error_local->message);
				} else {
					priv->model = val;
				}
			}
		}
		tmp = g_udev_device_get_property(udev_parent, "HID_NAME");
		if (tmp != NULL) {
			if (fu_device_get_name(device) == NULL)
				fu_device_set_name(device, tmp);
		}
	}

	/* if the device is a GPU try to fetch it from vbios_version */
	if (g_strcmp0(priv->subsystem, "pci") == 0 &&
	    fu_udev_device_is_pci_base_cls(FU_UDEV_DEVICE(device), FU_PCI_BASE_CLS_DISPLAY) &&
	    fu_device_get_version(device) == NULL) {
		const gchar *version;

		version = g_udev_device_get_sysfs_attr(priv->udev_device, "vbios_version");
		if (version != NULL) {
			fu_device_set_version(device, version);
			fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_add_icon(FU_DEVICE(self), "video-display");
		}
	}

	/* set the version if the revision has been set */
	if (fu_device_get_version(device) == NULL &&
	    fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		if (priv->revision != 0x00 && priv->revision != 0xFF) {
			g_autofree gchar *version = g_strdup_printf("%02x", priv->revision);
			fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_set_version(device, version);
		}
	}

	/* set model */
	if (fu_device_get_name(device) == NULL) {
		tmp = g_udev_device_get_property(priv->udev_device, "ID_MODEL_FROM_DATABASE");
		if (tmp == NULL)
			tmp = g_udev_device_get_property(priv->udev_device, "ID_MODEL");
		if (tmp == NULL)
			tmp = g_udev_device_get_property(priv->udev_device,
							 "ID_PCI_CLASS_FROM_DATABASE");
		if (tmp != NULL)
			fu_device_set_name(device, tmp);
	}

	/* set vendor */
	if (fu_device_get_vendor(device) == NULL) {
		tmp = fu_udev_device_get_vendor_fallback(priv->udev_device);
		if (tmp != NULL)
			fu_device_set_vendor(device, tmp);
	}

	/* set number */
	if (g_udev_device_get_number(priv->udev_device) != NULL) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_strtoull(g_udev_device_get_number(priv->udev_device),
				 &priv->number,
				 0x0,
				 G_MAXUINT64,
				 FU_INTEGER_BASE_AUTO,
				 &error_local)) {
			g_warning("failed to convert udev number: %s", error_local->message);
		}
	}

	/* try harder to find a vendor name the user will recognize */
	if (priv->flags & FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT && udev_parent != NULL &&
	    fu_device_get_vendor(device) == NULL) {
		g_autoptr(GUdevDevice) device_tmp = g_object_ref(udev_parent);
		for (guint i = 0; i < 0xff; i++) {
			g_autoptr(GUdevDevice) parent = NULL;
			tmp = fu_udev_device_get_vendor_fallback(device_tmp);
			if (tmp != NULL) {
				fu_device_set_vendor(device, tmp);
				break;
			}
			parent = g_udev_device_get_parent(device_tmp);
			if (parent == NULL)
				break;
			g_set_object(&device_tmp, parent);
		}
	}

	/* set revision */
	if (fu_device_get_version(device) == NULL &&
	    fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		tmp = g_udev_device_get_property(priv->udev_device, "ID_REVISION");
		if (tmp != NULL)
			fu_device_set_version(device, tmp);
	}

	/* set vendor ID */
	if (priv->subsystem != NULL)
		subsystem = g_ascii_strup(priv->subsystem, -1);
	if (subsystem != NULL && priv->vendor != 0x0000) {
		g_autofree gchar *vendor_id = NULL;
		vendor_id = g_strdup_printf("%s:0x%04X", subsystem, (guint)priv->vendor);
		fu_device_add_vendor_id(device, vendor_id);
	}

	/* add GUIDs in order of priority */
	if (priv->vendor != 0x0000)
		fu_device_add_instance_u16(device, "VEN", priv->vendor);
	if (priv->model != 0x0000)
		fu_device_add_instance_u16(device, "DEV", priv->model);
	if (priv->subsystem_vendor != 0x0000 || priv->subsystem_model != 0x0000) {
		g_autofree gchar *subsys =
		    g_strdup_printf("%04X%04X", priv->subsystem_vendor, priv->subsystem_model);
		fu_device_add_instance_str(device, "SUBSYS", subsys);
	}
	if (fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_ADD_INSTANCE_ID_REV) &&
	    priv->revision != 0xFF) {
		fu_device_add_instance_u8(device, "REV", priv->revision);
	}
	if (subsystem != NULL) {
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem,
						 "VEN",
						 NULL);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem,
						 "VEN",
						 "DEV",
						 NULL);
		if (fu_device_has_internal_flag(device,
						FU_DEVICE_INTERNAL_FLAG_ADD_INSTANCE_ID_REV)) {
			fu_device_build_instance_id_full(device,
							 FU_DEVICE_INSTANCE_FLAG_GENERIC |
							     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
							     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
							 NULL,
							 subsystem,
							 "VEN",
							 "DEV",
							 "REV",
							 NULL);
		}
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem,
						 "VEN",
						 "DEV",
						 "SUBSYS",
						 NULL);
		if (fu_device_has_internal_flag(device,
						FU_DEVICE_INTERNAL_FLAG_ADD_INSTANCE_ID_REV)) {
			fu_device_build_instance_id_full(device,
							 FU_DEVICE_INSTANCE_FLAG_GENERIC |
							     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
							     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
							 NULL,
							 subsystem,
							 "VEN",
							 "DEV",
							 "SUBSYS",
							 "REV",
							 NULL);
		}
	}

	/* set serial */
	if (!fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_NO_SERIAL_NUMBER) &&
	    fu_device_get_serial(device) == NULL) {
		tmp = g_udev_device_get_property(priv->udev_device, "ID_SERIAL_SHORT");
		if (tmp == NULL)
			tmp = g_udev_device_get_property(priv->udev_device, "ID_SERIAL");
		if (tmp != NULL)
			fu_device_set_serial(device, tmp);
	}

	/* add device class */
	if (subsystem != NULL) {
		tmp = g_udev_device_get_sysfs_attr(priv->udev_device, "class");
		if (tmp != NULL && g_str_has_prefix(tmp, "0x"))
			tmp += 2;
		fu_device_add_instance_strup(device, "CLASS", tmp);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem,
						 "VEN",
						 "CLASS",
						 NULL);

		/* add devtype */
		fu_device_add_instance_strup(device,
					     "TYPE",
					     g_udev_device_get_devtype(priv->udev_device));
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem,
						 "TYPE",
						 NULL);

		/* add the driver */
		fu_device_add_instance_str(device, "DRIVER", priv->driver);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem,
						 "DRIVER",
						 NULL);
	}

	/* add firmware_id */
	if (g_strcmp0(g_udev_device_get_subsystem(priv->udev_device), "serio") == 0) {
		if (!fu_udev_device_probe_serio(self, error))
			return FALSE;
	}

	/* determine if we're wired internally */
	parent_i2c = g_udev_device_get_parent_with_subsystem(priv->udev_device, "i2c", NULL);
	if (parent_i2c != NULL)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
#endif

	/* success */
	return TRUE;
}

#ifdef HAVE_GUDEV
static gchar *
fu_udev_device_get_miscdev0(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *fn;
	g_autofree gchar *miscdir = NULL;
	g_autoptr(GDir) dir = NULL;

	miscdir = g_build_filename(g_udev_device_get_sysfs_path(priv->udev_device), "misc", NULL);
	dir = g_dir_open(miscdir, 0, NULL);
	if (dir == NULL)
		return NULL;
	fn = g_dir_read_name(dir);
	if (fn == NULL)
		return NULL;
	return g_strdup_printf("/dev/%s", fn);
}

static void
fu_udev_device_set_dev_internal(FuUdevDevice *self, GUdevDevice *udev_device)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	if (g_set_object(&priv->udev_device, udev_device))
		g_object_notify(G_OBJECT(self), "udev-device");
}
#endif

/**
 * fu_udev_device_set_dev:
 * @self: a #FuUdevDevice
 * @udev_device: a #GUdevDevice
 *
 * Sets the #GUdevDevice. This may need to be used to replace the actual device
 * used for reads and writes before the device is probed.
 *
 * Since: 1.6.2
 **/
void
fu_udev_device_set_dev(FuUdevDevice *self, GUdevDevice *udev_device)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
#ifdef HAVE_GUDEV
	const gchar *summary;
#endif

	g_return_if_fail(FU_IS_UDEV_DEVICE(self));

#ifdef HAVE_GUDEV
	/* the net subsystem is not a real hardware class */
	if (udev_device != NULL &&
	    g_strcmp0(g_udev_device_get_subsystem(udev_device), "net") == 0) {
		g_autoptr(GUdevDevice) udev_device_phys = NULL;
		udev_device_phys = g_udev_device_get_parent(udev_device);
		fu_udev_device_set_dev_internal(self, udev_device_phys);
		fu_device_set_metadata(FU_DEVICE(self),
				       "ParentSubsystem",
				       g_udev_device_get_subsystem(udev_device));
	} else {
		fu_udev_device_set_dev_internal(self, udev_device);
	}
#endif

	/* set new device */
	if (priv->udev_device == NULL)
		return;
#ifdef HAVE_GUDEV
	fu_udev_device_set_subsystem(self, g_udev_device_get_subsystem(priv->udev_device));
	fu_udev_device_set_driver(self, g_udev_device_get_driver(priv->udev_device));
	fu_udev_device_set_device_file(self, g_udev_device_get_device_file(priv->udev_device));
	fu_udev_device_set_devtype(self, g_udev_device_get_devtype(priv->udev_device));

	/* so we can display something sensible for unclaimed devices */
	fu_device_set_backend_id(FU_DEVICE(self), g_udev_device_get_sysfs_path(priv->udev_device));

	/* fall back to the first thing handled by misc drivers */
	if (priv->device_file == NULL) {
		/* perhaps we should unconditionally fall back? or perhaps
		 * require FU_UDEV_DEVICE_FLAG_FALLBACK_MISC... */
		if (g_strcmp0(priv->subsystem, "serio") == 0)
			priv->device_file = fu_udev_device_get_miscdev0(self);
		if (priv->device_file != NULL)
			g_debug("falling back to misc %s", priv->device_file);
	}

	/* try to get one line summary */
	summary = g_udev_device_get_sysfs_attr(priv->udev_device, "description");
	if (summary == NULL) {
		g_autoptr(GUdevDevice) parent = NULL;
		parent = g_udev_device_get_parent(priv->udev_device);
		if (parent != NULL)
			summary = g_udev_device_get_sysfs_attr(parent, "description");
	}
	if (summary != NULL)
		fu_device_set_summary(FU_DEVICE(self), summary);
#endif
}

/**
 * fu_udev_device_get_subsystem_depth:
 * @self: a #FuUdevDevice
 * @subsystem: a subsystem
 *
 * Determine how far up a chain a given device is
 *
 * Returns: unsigned integer
 *
 * Since: 2.0.0
 **/
guint
fu_udev_device_get_subsystem_depth(FuUdevDevice *self, const gchar *subsystem)
{
	g_autoptr(FuUdevDevice) device_tmp = NULL;

	device_tmp = fu_udev_device_get_parent_with_subsystem(self,
							      subsystem,
							      NULL, /* devtype */
							      NULL);
	if (device_tmp == NULL)
		return 0;
	for (guint i = 0;; i++) {
		g_autoptr(FuUdevDevice) parent =
		    fu_udev_device_get_parent_with_subsystem(device_tmp,
							     NULL, /* subsystem */
							     NULL, /* devtype */
							     NULL);
		if (parent == NULL)
			return i;
		g_set_object(&device_tmp, parent);
	}
	return 0;
}

static void
fu_udev_device_probe_complete(FuDevice *device)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* free memory */
	g_clear_object(&priv->udev_device);
	priv->udev_device_cleared = TRUE;
}

static gboolean
fu_udev_device_unbind_driver(FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GOutputStream) stream = NULL;

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* is already unbound */
	if (fu_udev_device_get_sysfs_path(self) == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "not initialized");
		return FALSE;
	}
	fn = g_build_filename(fu_udev_device_get_sysfs_path(self), "driver", "unbind", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS))
		return TRUE;

	/* write bus ID to file */
	if (!fu_udev_device_ensure_bind_id(self, error))
		return FALSE;
	file = g_file_new_for_path(fn);
	stream =
	    G_OUTPUT_STREAM(g_file_replace(file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error));
	if (stream == NULL)
		return FALSE;
	return g_output_stream_write_all(stream,
					 priv->bind_id,
					 strlen(priv->bind_id),
					 NULL,
					 NULL,
					 error);
}

static gboolean
fu_udev_device_bind_driver(FuDevice *device,
			   const gchar *subsystem,
			   const gchar *driver,
			   GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *driver_safe = g_strdup(driver);
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GOutputStream) stream = NULL;

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* copy the logic from modprobe */
	g_strdelimit(driver_safe, "-", '_');

	/* driver exists */
	fn = g_strdup_printf("/sys/module/%s/drivers/%s:%s/bind",
			     driver_safe,
			     subsystem,
			     driver_safe);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot bind with %s:%s",
			    subsystem,
			    driver);
		return FALSE;
	}

	/* write bus ID to file */
	if (!fu_udev_device_ensure_bind_id(self, error))
		return FALSE;
	if (priv->bind_id == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "bind-id not set for subsystem %s",
			    priv->subsystem);
		return FALSE;
	}
	file = g_file_new_for_path(fn);
	stream =
	    G_OUTPUT_STREAM(g_file_replace(file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error));
	if (stream == NULL)
		return FALSE;
	return g_output_stream_write_all(stream,
					 priv->bind_id,
					 strlen(priv->bind_id),
					 NULL,
					 NULL,
					 error);
}

static void
fu_udev_device_incorporate(FuDevice *self, FuDevice *donor)
{
	FuUdevDevice *uself = FU_UDEV_DEVICE(self);
	FuUdevDevice *udonor = FU_UDEV_DEVICE(donor);
	FuUdevDevicePrivate *priv = GET_PRIVATE(uself);
	FuUdevDevicePrivate *priv_donor = GET_PRIVATE(udonor);

	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	g_return_if_fail(FU_IS_UDEV_DEVICE(donor));

	fu_udev_device_set_dev(uself, fu_udev_device_get_dev(udonor));
	if (priv->device_file == NULL) {
		fu_udev_device_set_subsystem(uself, fu_udev_device_get_subsystem(udonor));
		fu_udev_device_set_bind_id(uself, fu_udev_device_get_bind_id(udonor));
		fu_udev_device_set_device_file(uself, fu_udev_device_get_device_file(udonor));
		fu_udev_device_set_devtype(uself, fu_udev_device_get_devtype(udonor));
		fu_udev_device_set_driver(uself, fu_udev_device_get_driver(udonor));
	}
	if (priv->vendor == 0x0 && priv_donor->vendor != 0x0)
		priv->vendor = priv_donor->vendor;
	if (priv->model == 0x0 && priv_donor->model != 0x0)
		priv->model = priv_donor->model;
	if (priv->subsystem_vendor == 0x0 && priv_donor->subsystem_vendor != 0x0)
		priv->subsystem_vendor = priv_donor->subsystem_vendor;
	if (priv->subsystem_model == 0x0 && priv_donor->subsystem_model != 0x0)
		priv->subsystem_model = priv_donor->subsystem_model;
	if (priv->revision == 0x0 && priv_donor->revision != 0x0)
		priv->revision = priv_donor->revision;
	if (priv->number == 0x0 && priv_donor->number != 0x0)
		priv->number = priv_donor->number;
}

/**
 * fu_udev_device_get_dev:
 * @self: a #FuUdevDevice
 *
 * Gets the #GUdevDevice.
 *
 * NOTE: If a plugin calls this after the `->probe()` and `->setup()` phases then the
 * %FU_DEVICE_INTERNAL_FLAG_NO_PROBE_COMPLETE flag should be set on the device to avoid a warning.
 *
 * Returns: (transfer none): a #GUdevDevice, or %NULL
 *
 * Since: 1.1.2
 **/
GUdevDevice *
fu_udev_device_get_dev(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
#ifndef SUPPORTED_BUILD
	if (priv->udev_device == NULL && priv->udev_device_cleared) {
		g_autofree gchar *str = fu_device_to_string(FU_DEVICE(self));
		g_warning("GUdevDevice is not available post-probe, use "
			  "FU_DEVICE_INTERNAL_FLAG_NO_PROBE_COMPLETE in %s plugin to opt-out: %s",
			  fu_device_get_plugin(FU_DEVICE(self)),
			  str);
	}
#endif
	return priv->udev_device;
}

/**
 * fu_udev_device_get_subsystem:
 * @self: a #FuUdevDevice
 *
 * Gets the device subsystem, e.g. `pci`
 *
 * Returns: a subsystem, or NULL if unset or invalid
 *
 * Since: 1.1.2
 **/
const gchar *
fu_udev_device_get_subsystem(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	return priv->subsystem;
}

/**
 * fu_udev_device_get_bind_id:
 * @self: a #FuUdevDevice
 *
 * Gets the device ID used for binding the device, e.g. `pci:1:2:3`
 *
 * Returns: a bind_id, or NULL if unset or invalid
 *
 * Since: 1.7.2
 **/
const gchar *
fu_udev_device_get_bind_id(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	fu_udev_device_ensure_bind_id(self, NULL);
	return priv->bind_id;
}

/**
 * fu_udev_device_get_driver:
 * @self: a #FuUdevDevice
 *
 * Gets the device driver, e.g. `psmouse`.
 *
 * Returns: a subsystem, or NULL if unset or invalid
 *
 * Since: 1.5.3
 **/
const gchar *
fu_udev_device_get_driver(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	return priv->driver;
}

/**
 * fu_udev_device_get_device_file:
 * @self: a #FuUdevDevice
 *
 * Gets the device node.
 *
 * Returns: a device file, or NULL if unset
 *
 * Since: 1.3.1
 **/
const gchar *
fu_udev_device_get_device_file(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	return priv->device_file;
}

/**
 * fu_udev_device_get_sysfs_path:
 * @self: a #FuUdevDevice
 *
 * Gets the device sysfs path, e.g. `/sys/devices/pci0000:00/0000:00:14.0`.
 *
 * Returns: a local path, or NULL if unset or invalid
 *
 * Since: 1.1.2
 **/
const gchar *
fu_udev_device_get_sysfs_path(FuUdevDevice *self)
{
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	return fu_device_get_backend_id(FU_DEVICE(self));
}

/**
 * fu_udev_device_get_number:
 * @self: a #FuUdevDevice
 *
 * Gets the device number, if any.
 *
 * Returns: integer, 0 if the data is unavailable.
 *
 * Since: 1.5.0
 **/
guint64
fu_udev_device_get_number(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0);
	return priv->number;
}

/**
 * fu_udev_device_is_pci_base_cls:
 * @self: a #FuUdevDevice
 * @cls: #FuPciBaseCls type
 *
 * Determines whether the device matches a given pci base class type
 *
 * Since: 1.8.11
 **/
gboolean
fu_udev_device_is_pci_base_cls(FuUdevDevice *self, FuPciBaseCls cls)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	return (priv->class >> 16) == cls;
}

/**
 * fu_udev_device_get_cls:
 * @self: a #FuUdevDevice
 *
 * Gets the PCI class for a device.
 *
 * The class consists of a base class and subclass.
 *
 * Returns: a PCI class
 *
 * Since: 1.8.11
 **/
guint32
fu_udev_device_get_cls(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0x0000);
	return priv->class;
}

/**
 * fu_udev_device_get_vendor:
 * @self: a #FuUdevDevice
 *
 * Gets the device vendor code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint16
fu_udev_device_get_vendor(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0x0000);
	return priv->vendor;
}

/**
 * fu_udev_device_get_model:
 * @self: a #FuUdevDevice
 *
 * Gets the device model code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint16
fu_udev_device_get_model(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0x0000);
	return priv->model;
}

/**
 * fu_udev_device_get_subsystem_vendor:
 * @self: a #FuUdevDevice
 *
 * Gets the device subsystem vendor code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.5.0
 **/
guint16
fu_udev_device_get_subsystem_vendor(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0x0000);
	return priv->subsystem_vendor;
}

/**
 * fu_udev_device_get_subsystem_model:
 * @self: a #FuUdevDevice
 *
 * Gets the device subsystem model code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.5.0
 **/
guint16
fu_udev_device_get_subsystem_model(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0x0000);
	return priv->subsystem_model;
}

/**
 * fu_udev_device_get_revision:
 * @self: a #FuUdevDevice
 *
 * Gets the device revision.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 1.1.2
 **/
guint8
fu_udev_device_get_revision(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), 0x00);
	return priv->revision;
}

#ifdef HAVE_GUDEV
static gchar *
fu_udev_device_get_parent_subsystems(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	GString *str = g_string_new(NULL);
	g_autoptr(GUdevDevice) udev_device = g_object_ref(priv->udev_device);

	/* find subsystems of self and all parent devices */
	if (priv->subsystem != NULL)
		g_string_append_printf(str, "%s,", priv->subsystem);
	while (TRUE) {
		g_autoptr(GUdevDevice) parent = g_udev_device_get_parent(udev_device);
		if (parent == NULL)
			break;
		if (g_udev_device_get_subsystem(parent) != NULL) {
			g_string_append_printf(str, "%s,", g_udev_device_get_subsystem(parent));
		}
		g_set_object(&udev_device, parent);
	}
	if (str->len > 0)
		g_string_truncate(str, str->len - 1);
	return g_string_free(str, FALSE);
}

static gboolean
fu_udev_device_match_subsystem_devtype(GUdevDevice *udev_device,
				       const gchar *subsystem,
				       const gchar *devtype)
{
	if (subsystem != NULL) {
		if (g_strcmp0(g_udev_device_get_subsystem(udev_device), subsystem) != 0)
			return FALSE;
	}
	if (devtype != NULL) {
		if (g_strcmp0(g_udev_device_get_devtype(udev_device), devtype) != 0)
			return FALSE;
	}
	return TRUE;
}
#endif

/**
 * fu_udev_device_set_physical_id:
 * @self: a #FuUdevDevice
 * @subsystems: a subsystem string, e.g. `pci,usb,scsi:scsi_target`
 * @error: (nullable): optional return location for an error
 *
 * Sets the physical ID from the device subsystem. Plugins should choose the
 * subsystem that is "deepest" in the udev tree, for instance choosing `usb`
 * over `pci` for a mouse device.
 *
 * The devtype can also be specified for a specific device, which is useful when the
 * subsystem alone is not enough to identify the physical device. e.g. ignoring the
 * specific LUNs for a SCSI device.
 *
 * Returns: %TRUE if the physical device was set.
 *
 * Since: 1.1.2
 **/
gboolean
fu_udev_device_set_physical_id(FuUdevDevice *self, const gchar *subsystems, GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	g_autofree gchar *physical_id = NULL;
	g_autofree gchar *subsystem = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GUdevDevice) udev_device = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(subsystems != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* nothing to do */
	if (priv->udev_device == NULL)
		return TRUE;

	/* look for each subsystem[:devtype] in turn */
	split = g_strsplit(subsystems, ",", -1);
	for (guint i = 0; split[i] != NULL; i++) {
		g_auto(GStrv) subsys_devtype = g_strsplit(split[i], ":", 2);
		g_autoptr(FuUdevDevice) device_parent = NULL;

		/* matching on devtype is optional */
		device_parent = fu_udev_device_get_parent_with_subsystem(self,
									 subsys_devtype[0],
									 subsys_devtype[1],
									 NULL);
		if (device_parent != NULL) {
			subsystem = g_strdup(subsys_devtype[0]);
			udev_device = g_object_ref(fu_udev_device_get_dev(device_parent));
			break;
		}
	}
	if (udev_device == NULL) {
		g_autofree gchar *str = fu_udev_device_get_parent_subsystems(self);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "failed to find device with subsystems %s, only got %s",
			    subsystems,
			    str);
		return FALSE;
	}

	if (g_strcmp0(subsystem, "pci") == 0) {
		tmp = g_udev_device_get_property(udev_device, "PCI_SLOT_NAME");
		if (tmp == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "failed to find PCI_SLOT_NAME");
			return FALSE;
		}
		physical_id = g_strdup_printf("PCI_SLOT_NAME=%s", tmp);
	} else if (g_strcmp0(subsystem, "usb") == 0 || g_strcmp0(subsystem, "mmc") == 0 ||
		   g_strcmp0(subsystem, "i2c") == 0 || g_strcmp0(subsystem, "platform") == 0 ||
		   g_strcmp0(subsystem, "scsi") == 0 || g_strcmp0(subsystem, "mtd") == 0 ||
		   g_strcmp0(subsystem, "block") == 0 || g_strcmp0(subsystem, "gpio") == 0 ||
		   g_strcmp0(subsystem, "video4linux") == 0) {
		tmp = g_udev_device_get_property(udev_device, "DEVPATH");
		if (tmp == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "failed to find DEVPATH");
			return FALSE;
		}
		physical_id = g_strdup_printf("DEVPATH=%s", tmp);
	} else if (g_strcmp0(subsystem, "hid") == 0) {
		tmp = g_udev_device_get_property(udev_device, "HID_PHYS");
		if (tmp == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "failed to find HID_PHYS");
			return FALSE;
		}
		physical_id = g_strdup_printf("HID_PHYS=%s", tmp);
	} else if (g_strcmp0(subsystem, "tpm") == 0 ||
		   g_strcmp0(subsystem, "drm_dp_aux_dev") == 0) {
		tmp = g_udev_device_get_property(udev_device, "DEVNAME");
		if (tmp == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "failed to find DEVNAME");
			return FALSE;
		}
		physical_id = g_strdup_printf("DEVNAME=%s", tmp);
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot handle subsystem %s",
			    subsystem);
		return FALSE;
	}

	/* success */
	fu_device_set_physical_id(FU_DEVICE(self), physical_id);
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <gudev.h> is unavailable");
	return FALSE;
#endif
}

/**
 * fu_udev_device_set_logical_id:
 * @self: a #FuUdevDevice
 * @subsystem: a subsystem string, e.g. `pci,usb`
 * @error: (nullable): optional return location for an error
 *
 * Sets the logical ID from the device subsystem. Plugins should choose the
 * subsystem that most relevant in the udev tree, for instance choosing `hid`
 * over `usb` for a mouse device.
 *
 * Returns: %TRUE if the logical device was set.
 *
 * Since: 1.5.8
 **/
gboolean
fu_udev_device_set_logical_id(FuUdevDevice *self, const gchar *subsystem, GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	g_autofree gchar *logical_id = NULL;
	g_autoptr(GUdevDevice) udev_device = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(subsystem != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* nothing to do */
	if (priv->udev_device == NULL)
		return TRUE;

	/* find correct device matching subsystem */
	if (g_strcmp0(priv->subsystem, subsystem) == 0) {
		udev_device = g_object_ref(priv->udev_device);
	} else {
		udev_device =
		    g_udev_device_get_parent_with_subsystem(priv->udev_device, subsystem, NULL);
	}
	if (udev_device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "failed to find device with subsystem %s",
			    subsystem);
		return FALSE;
	}

	/* query each subsystem */
	if (g_strcmp0(subsystem, "hid") == 0) {
		tmp = g_udev_device_get_property(udev_device, "HID_UNIQ");
		if (tmp == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "failed to find HID_UNIQ");
			return FALSE;
		}
		logical_id = g_strdup_printf("HID_UNIQ=%s", tmp);
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot handle subsystem %s",
			    subsystem);
		return FALSE;
	}

	/* success */
	fu_device_set_logical_id(FU_DEVICE(self), logical_id);
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <gudev.h> is unavailable");
	return FALSE;
#endif
}

/**
 * fu_udev_device_get_io_channel:
 * @self: a #FuUdevDevice
 *
 * Gets the IO channel.
 *
 * Returns: (transfer none): a #FuIOChannel, or %NULL if the device is not open
 *
 * Since: 1.9.8
 **/
FuIOChannel *
fu_udev_device_get_io_channel(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	return priv->io_channel;
}

/**
 * fu_udev_device_set_io_channel:
 * @self: a #FuUdevDevice
 * @io_channel: a #FuIOChannel
 *
 * Replace the IO channel to use when the device has already been opened.
 * This object will automatically unref @io_channel when fu_device_close() is called.
 *
 * Since: 1.9.8
 **/
void
fu_udev_device_set_io_channel(FuUdevDevice *self, FuIOChannel *io_channel)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	g_return_if_fail(FU_IS_IO_CHANNEL(io_channel));
	g_set_object(&priv->io_channel, io_channel);
}

/**
 * fu_udev_device_remove_flag:
 * @self: a #FuUdevDevice
 * @flag: udev device flag, e.g. %FU_UDEV_DEVICE_FLAG_OPEN_READ
 *
 * Removes a parameters flag.
 *
 * Since: 2.0.0
 **/
void
fu_udev_device_remove_flag(FuUdevDevice *self, FuUdevDeviceFlags flag)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	priv->flags &= ~flag;
}

/**
 * fu_udev_device_add_flag:
 * @self: a #FuUdevDevice
 * @flag: udev device flag, e.g. %FU_UDEV_DEVICE_FLAG_OPEN_READ
 *
 * Sets the parameters to use when opening the device.
 *
 * For example %FU_UDEV_DEVICE_FLAG_OPEN_READ means that fu_device_open()
 * would use `O_RDONLY` rather than `O_RDWR` which is the default.
 *
 * Since: 2.0.0
 **/
void
fu_udev_device_add_flag(FuUdevDevice *self, FuUdevDeviceFlags flag)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));

	/* already set */
	if (priv->flags & flag)
		return;
	priv->flags |= flag;
}

static gboolean
fu_udev_device_open(FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	gint fd;

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* old versions of fwupd used to start with OPEN_READ|OPEN_WRITE and then plugins
	 * could add more flags, or set the flags back to NONE -- detect and fixup */
	if (priv->device_file != NULL && priv->flags == FU_UDEV_DEVICE_FLAG_NONE) {
#ifndef SUPPORTED_BUILD
		g_critical("%s [%s] forgot to call fu_udev_device_add_flag() with OPEN_READ and/or "
			   "OPEN_WRITE",
			   fu_device_get_name(device),
			   fu_device_get_id(device));
#endif
		fu_udev_device_add_flag(FU_UDEV_DEVICE(self), FU_UDEV_DEVICE_FLAG_OPEN_READ);
		fu_udev_device_add_flag(FU_UDEV_DEVICE(self), FU_UDEV_DEVICE_FLAG_OPEN_WRITE);
	}

	/* open device */
	if (priv->device_file != NULL && priv->flags != FU_UDEV_DEVICE_FLAG_NONE) {
		gint flags;
		g_autoptr(FuIOChannel) io_channel = NULL;
		if (priv->flags & FU_UDEV_DEVICE_FLAG_OPEN_READ &&
		    priv->flags & FU_UDEV_DEVICE_FLAG_OPEN_WRITE) {
			flags = O_RDWR;
		} else if (priv->flags & FU_UDEV_DEVICE_FLAG_OPEN_WRITE) {
			flags = O_WRONLY;
		} else {
			flags = O_RDONLY;
		}
#ifdef O_NONBLOCK
		if (priv->flags & FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK)
			flags |= O_NONBLOCK;
#endif
#ifdef O_SYNC
		if (priv->flags & FU_UDEV_DEVICE_FLAG_OPEN_SYNC)
			flags |= O_SYNC;
#endif
		fd = g_open(priv->device_file, flags, 0);
		if (fd < 0) {
			g_set_error(error,
				    G_IO_ERROR, /* nocheck */
#ifdef HAVE_ERRNO_H
				    g_io_error_from_errno(errno),
#else
				    G_IO_ERROR_FAILED, /* nocheck */
#endif
				    "failed to open %s: %s",
				    priv->device_file,
				    g_strerror(errno));
			fwupd_error_convert(error);
			return FALSE;
		}
		io_channel = fu_io_channel_unix_new(fd);
		g_set_object(&priv->io_channel, io_channel);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_udev_device_rescan(FuDevice *device, GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *sysfs_path;
	g_autoptr(GUdevClient) udev_client = g_udev_client_new(NULL);
	g_autoptr(GUdevDevice) udev_device = NULL;

	/* never set */
	if (priv->udev_device == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "rescan with no previous device");
		return FALSE;
	}
	sysfs_path = g_udev_device_get_sysfs_path(priv->udev_device);
	udev_device = g_udev_client_query_by_sysfs_path(udev_client, sysfs_path);
	if (udev_device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "rescan could not find device %s",
			    sysfs_path);
		return FALSE;
	}
	fu_udev_device_set_dev(self, udev_device);
	fu_device_probe_invalidate(device);
#endif
	return fu_device_probe(device, error);
}

static gboolean
fu_udev_device_close(FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* optional */
	if (priv->io_channel != NULL) {
		if (!fu_io_channel_shutdown(priv->io_channel, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_udev_device_ioctl:
 * @self: a #FuUdevDevice
 * @request: request number
 * @buf: a buffer to use, which *must* be large enough for the request
 * @bufsz: the size of @buf
 * @rc: (out) (nullable): the raw return value from the ioctl
 * @timeout: timeout in ms for the retry action, see %FU_UDEV_DEVICE_FLAG_IOCTL_RETRY
 * @error: (nullable): optional return location for an error
 *
 * Control a device using a low-level request.
 *
 * NOTE: In version 2.0.0 the @bufsz parameter was added -- which isn't required to perform the
 * ioctl, but *is* required to accurately track and emulate the device buffer.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_udev_device_ioctl(FuUdevDevice *self,
		     gulong request,
		     guint8 *buf,
		     gsize bufsz,
		     gint *rc,
		     guint timeout,
		     GError **error)
{
#ifdef HAVE_IOCTL_H
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	gint rc_tmp;
	g_autoptr(GTimer) timer = g_timer_new();
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(request != 0x0, FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		g_autofree gchar *buf_base64 = g_base64_encode(buf, bufsz);
		event_id = g_strdup_printf("Ioctl:"
					   "Request=0x%04x,"
					   "Data=%s,"
					   "Length=0x%x",
					   (guint)request,
					   buf_base64,
					   (guint)bufsz);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return FALSE;
		return fu_device_event_copy_data(event, "Data", buf, bufsz, error);
	}

	/* save */
	if (event_id != NULL) {
		event = fu_device_save_event(FU_DEVICE(self), event_id);
		fu_device_event_set_data(event, "Data", buf, bufsz);
	}

	/* not open! */
	if (priv->io_channel == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s [%s] has not been opened",
			    fu_device_get_id(FU_DEVICE(self)),
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}

	/* poll if required  up to the timeout */
	do {
		rc_tmp = ioctl(fu_io_channel_unix_get_fd(priv->io_channel), request, buf);
		if (rc_tmp >= 0)
			break;
	} while ((priv->flags & FU_UDEV_DEVICE_FLAG_IOCTL_RETRY) &&
		 (errno == EINTR || errno == EAGAIN) &&
		 g_timer_elapsed(timer, NULL) < timeout * 1000.f);
	if (rc != NULL)
		*rc = rc_tmp;
	if (rc_tmp < 0) {
#ifdef HAVE_ERRNO_H
		if (errno == EPERM) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_PERMISSION_DENIED,
					    "permission denied");
			return FALSE;
		}
		if (errno == ENOTTY) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "permission denied");
			return FALSE;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "ioctl error: %s [%i]",
			    g_strerror(errno),
			    errno);
#else
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "unspecified ioctl error");
#endif
		return FALSE;
	}

	/* save response */
	if (event != NULL)
		fu_device_event_set_data(event, "DataOut", buf, bufsz);

	/* success */
	return TRUE;
#else
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "Not supported as <sys/ioctl.h> not found");
	return FALSE;
#endif
}

/**
 * fu_udev_device_pread:
 * @self: a #FuUdevDevice
 * @port: offset address
 * @buf: (in): data
 * @bufsz: size of @buf
 * @error: (nullable): optional return location for an error
 *
 * Read a buffer from a file descriptor at a given offset.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_udev_device_pread(FuUdevDevice *self, goffset port, guint8 *buf, gsize bufsz, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		g_autofree gchar *buf_base64 = g_base64_encode(buf, bufsz);
		event_id = g_strdup_printf("Pread:"
					   "Port=0x%x,"
					   "Length=0x%x",
					   (guint)port,
					   (guint)bufsz);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return FALSE;
		return fu_device_event_copy_data(event, "Data", buf, bufsz, error);
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	/* not open! */
	if (priv->io_channel == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s [%s] has not been opened",
			    fu_device_get_id(FU_DEVICE(self)),
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}

#ifdef HAVE_PWRITE
	if (pread(fu_io_channel_unix_get_fd(priv->io_channel), buf, bufsz, port) != (gssize)bufsz) {
		g_set_error(error,
			    G_IO_ERROR, /* nocheck */
#ifdef HAVE_ERRNO_H
			    g_io_error_from_errno(errno),
#else
			    G_IO_ERROR_FAILED, /* nocheck */
#endif
			    "failed to read from port 0x%04x: %s",
			    (guint)port,
			    g_strerror(errno));
		fwupd_error_convert(error);
		return FALSE;
	}

	/* save response */
	if (event != NULL)
		fu_device_event_set_data(event, "Data", buf, bufsz);
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as pread() is unavailable");
	return FALSE;
#endif
}

/**
 * fu_udev_device_seek:
 * @self: a #FuUdevDevice
 * @offset: offset address
 * @error: (nullable): optional return location for an error
 *
 * Seeks a file descriptor to a given offset.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.7.2
 **/
gboolean
fu_udev_device_seek(FuUdevDevice *self, goffset offset, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* not open! */
	if (priv->io_channel == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s [%s] has not been opened",
			    fu_device_get_id(FU_DEVICE(self)),
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}

#ifdef HAVE_PWRITE
	if (lseek(fu_io_channel_unix_get_fd(priv->io_channel), offset, SEEK_SET) < 0) {
		g_set_error(error,
			    G_IO_ERROR, /* nocheck */
#ifdef HAVE_ERRNO_H
			    g_io_error_from_errno(errno),
#else
			    G_IO_ERROR_FAILED, /* nocheck */
#endif
			    "failed to seek to 0x%04x: %s",
			    (guint)offset,
			    g_strerror(errno));
		fwupd_error_convert(error);
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as lseek() is unavailable");
	return FALSE;
#endif
}

/**
 * fu_udev_device_pwrite:
 * @self: a #FuUdevDevice
 * @port: offset address
 * @buf: (out): data
 * @bufsz: size of @data
 * @error: (nullable): optional return location for an error
 *
 * Write a buffer to a file descriptor at a given offset.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_udev_device_pwrite(FuUdevDevice *self,
		      goffset port,
		      const guint8 *buf,
		      gsize bufsz,
		      GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* not open! */
	if (priv->io_channel == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s [%s] has not been opened",
			    fu_device_get_id(FU_DEVICE(self)),
			    fu_device_get_name(FU_DEVICE(self)));
		return FALSE;
	}

#ifdef HAVE_PWRITE
	if (pwrite(fu_io_channel_unix_get_fd(priv->io_channel), buf, bufsz, port) !=
	    (gssize)bufsz) {
		g_set_error(error,
			    G_IO_ERROR, /* nocheck */
#ifdef HAVE_ERRNO_H
			    g_io_error_from_errno(errno),
#else
			    G_IO_ERROR_FAILED, /* nocheck */
#endif
			    "failed to write to port %04x: %s",
			    (guint)port,
			    g_strerror(errno));
		fwupd_error_convert(error);
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as pwrite() is unavailable");
	return FALSE;
#endif
}

/**
 * fu_udev_device_get_sysfs_attr:
 * @self: a #FuUdevDevice
 * @attr: name of attribute to get
 * @error: (nullable): optional return location for an error
 *
 * Reads an arbitrary sysfs attribute 'attr' associated with UDEV device
 *
 * Returns: string or NULL
 *
 * Since: 1.4.5
 **/
const gchar *
fu_udev_device_get_sysfs_attr(FuUdevDevice *self, const gchar *attr, GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *result;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	g_return_val_if_fail(attr != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* nothing to do */
	if (priv->udev_device == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "not initialized");
		return NULL;
	}
	result = g_udev_device_get_sysfs_attr(priv->udev_device, attr);
	if (result == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "attribute %s returned no data",
			    attr);
		return NULL;
	}

	return result;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "getting attributes is not supported as no GUdev support");
	return NULL;
#endif
}

/**
 * fu_udev_device_get_sysfs_attr_uint64:
 * @self: a #FuUdevDevice
 * @attr: name of attribute to get
 * @value: (out) (optional): value to return
 * @error: (nullable): optional return location for an error
 *
 * Reads an arbitrary sysfs attribute 'attr' associated with UDEV device as a uint64.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.7.2
 **/
gboolean
fu_udev_device_get_sysfs_attr_uint64(FuUdevDevice *self,
				     const gchar *attr,
				     guint64 *value,
				     GError **error)
{
	const gchar *tmp;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(attr != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	tmp = fu_udev_device_get_sysfs_attr(self, attr, error);
	if (tmp == NULL)
		return FALSE;
	return fu_strtoull(tmp, value, 0, G_MAXUINT64, FU_INTEGER_BASE_AUTO, error);
}

/**
 * fu_udev_device_write_sysfs:
 * @self: a #FuUdevDevice
 * @attr: sysfs attribute name
 * @val: data to write into the attribute
 * @timeout_ms: IO timeout in milliseconds
 * @error: (nullable): optional return location for an error
 *
 * Writes data into a sysfs attribute
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_udev_device_write_sysfs(FuUdevDevice *self,
			   const gchar *attr,
			   const gchar *val,
			   guint timeout_ms,
			   GError **error)
{
	g_autofree gchar *path = NULL;
	g_autoptr(FuIOChannel) io_channel = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), FALSE);
	g_return_val_if_fail(attr != NULL, FALSE);
	g_return_val_if_fail(val != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* open the file */
	if (fu_udev_device_get_sysfs_path(self) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "sysfs_path undefined");
		return FALSE;
	}
	path = g_build_filename(fu_udev_device_get_sysfs_path(self), attr, NULL);
	io_channel = fu_io_channel_new_file(path, FU_IO_CHANNEL_OPEN_FLAG_WRITE, error);
	if (io_channel == NULL)
		return FALSE;
	return fu_io_channel_write_raw(io_channel,
				       (const guint8 *)val,
				       strlen(val),
				       timeout_ms,
				       FU_IO_CHANNEL_FLAG_NONE,
				       error);
}

/**
 * fu_udev_device_get_devtype:
 * @self: a #FuUdevDevice
 *
 * Returns the Udev device type
 *
 * Returns: device type specified in the uevent
 *
 * Since: 1.4.5
 **/
const gchar *
fu_udev_device_get_devtype(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	return priv->devtype;
}

/**
 * fu_udev_device_get_siblings_with_subsystem
 * @self: a #FuUdevDevice
 * @subsystem: the name of a udev subsystem
 * @error: (nullable): optional return location for an error
 *
 * Get a list of devices that are siblings of self and have the
 * provided subsystem.
 *
 * Returns: (element-type FuUdevDevice) (transfer full): devices, or %NULL on error
 *
 * Since: 2.0.0
 */
GPtrArray *
fu_udev_device_get_siblings_with_subsystem(FuUdevDevice *self,
					   const gchar *subsystem,
					   GError **error)
{
	g_autoptr(GPtrArray) out = g_ptr_array_new_with_free_func(g_object_unref);

#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *udev_parent_path;
	g_autoptr(GUdevDevice) udev_parent = NULL;
	g_autoptr(GUdevClient) udev_client = g_udev_client_new(NULL);
	g_autolist(GUdevDevice) enumerated =
	    g_udev_client_query_by_subsystem(udev_client, subsystem);

	/* we have no parent, and so no siblings are possible */
	if (priv->udev_device == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "not initialized");
		return NULL;
	}
	udev_parent = g_udev_device_get_parent(priv->udev_device);
	if (udev_parent == NULL)
		return g_steal_pointer(&out);
	udev_parent_path = g_udev_device_get_sysfs_path(udev_parent);

	for (GList *element = enumerated; element != NULL; element = element->next) {
		GUdevDevice *enumerated_device = G_UDEV_DEVICE(element->data);
		g_autoptr(GUdevDevice) enumerated_parent = NULL;
		const gchar *enumerated_parent_path;

		/* get parent, if it exists */
		enumerated_parent = g_udev_device_get_parent(enumerated_device);
		if (enumerated_parent == NULL)
			break;
		enumerated_parent_path = g_udev_device_get_sysfs_path(enumerated_parent);

		/* if the sysfs path of self's parent is the same as that of the
		 * located device's parent, they are siblings */
		if (g_strcmp0(udev_parent_path, enumerated_parent_path) == 0) {
			g_ptr_array_add(out,
					fu_udev_device_new(fu_device_get_context(FU_DEVICE(self)),
							   enumerated_device));
		}
	}
#endif

	return g_steal_pointer(&out);
}

/**
 * fu_udev_device_get_parent_with_subsystem
 * @self: a #FuUdevDevice
 * @subsystem: (nullable): the name of a udev subsystem, or %NULL for any
 * @devtype: (nullable): the name of a udev devtype, e.g. `usb_interface`, or %NULL for any
 * @error: (nullable): optional return location for an error
 *
 * Get the device that is a ancestor of self and has the provided subsystem and device type.
 *
 * Returns: (transfer full): device, or %NULL
 *
 * Since: 2.0.0
 */
FuUdevDevice *
fu_udev_device_get_parent_with_subsystem(FuUdevDevice *self,
					 const gchar *subsystem,
					 const gchar *devtype,
					 GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GUdevDevice) device_tmp = NULL;

	/* sanity check */
	if (priv->udev_device == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "not initialized");
		return NULL;
	}
	device_tmp = g_udev_device_get_parent(priv->udev_device);
	while (device_tmp != NULL) {
		g_autoptr(GUdevDevice) parent = NULL;
		if (fu_udev_device_match_subsystem_devtype(device_tmp, subsystem, devtype))
			break;
		parent = g_udev_device_get_parent(device_tmp);
		g_set_object(&device_tmp, parent);
	}
	if (device_tmp == NULL) {
		if (devtype != NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no parent with subsystem %s and devtype %s",
				    subsystem,
				    devtype);
			return NULL;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no parent with subsystem %s",
			    subsystem);
		return NULL;
	}
	return fu_udev_device_new(fu_device_get_context(FU_DEVICE(self)), device_tmp);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "not supported as <gudev.h> is unavailable");
	return NULL;
#endif
}

/**
 * fu_udev_device_get_children_with_subsystem
 * @self: a #FuUdevDevice
 * @subsystem: the name of a udev subsystem
 *
 * Get a list of devices that are children of self and have the
 * provided subsystem.
 *
 * Returns: (element-type FuUdevDevice) (transfer full): devices
 *
 * Since: 1.6.2
 */
GPtrArray *
fu_udev_device_get_children_with_subsystem(FuUdevDevice *self, const gchar *const subsystem)
{
	g_autoptr(GPtrArray) out = g_ptr_array_new_with_free_func(g_object_unref);

#ifdef HAVE_GUDEV
	const gchar *self_path = fu_udev_device_get_sysfs_path(self);
	g_autoptr(GUdevClient) udev_client = g_udev_client_new(NULL);

	g_autoptr(GList) enumerated = g_udev_client_query_by_subsystem(udev_client, subsystem);
	for (GList *element = enumerated; element != NULL; element = element->next) {
		g_autoptr(GUdevDevice) enumerated_device = element->data;
		g_autoptr(GUdevDevice) enumerated_parent = NULL;
		const gchar *enumerated_parent_path;

		/* get parent, if it exists */
		enumerated_parent = g_udev_device_get_parent(enumerated_device);
		if (enumerated_parent == NULL)
			break;
		enumerated_parent_path = g_udev_device_get_sysfs_path(enumerated_parent);

		/* enumerated device is a child of self if its parent is the
		 * same as self */
		if (g_strcmp0(self_path, enumerated_parent_path) == 0) {
			FuUdevDevice *dev =
			    fu_udev_device_new(fu_device_get_context(FU_DEVICE(self)),
					       g_steal_pointer(&enumerated_device));
			g_ptr_array_add(out, dev);
		}
	}
#endif

	return g_steal_pointer(&out);
}

/**
 * fu_udev_device_find_usb_device:
 * @self: a #FuUdevDevice
 * @error: (nullable): optional return location for an error
 *
 * Gets the matching #GUsbDevice for the #GUdevDevice.
 *
 * NOTE: This should never be stored in the device class as an instance variable, as the lifecycle
 * for `GUsbDevice` may be different to the `FuUdevDevice`. Every time the `GUsbDevice` is used
 * this function should be called.
 *
 * Returns: (transfer full): a #FuUsbDevice, or NULL if unset or invalid
 *
 * Since: 1.8.7
 **/
FuDevice *
fu_udev_device_find_usb_device(FuUdevDevice *self, GError **error)
{
#if defined(HAVE_GUDEV) && defined(HAVE_GUSB)
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	guint8 bus = 0;
	guint8 address = 0;
	g_autoptr(GUdevDevice) udev_device = NULL;
	g_autoptr(GUsbContext) usb_ctx = NULL;
	g_autoptr(GUsbDevice) usb_device = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* sanity check */
	if (priv->udev_device == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "not initialized");
		return NULL;
	}

	/* look at the current device and all the parent devices until we can find the USB data */
	udev_device = g_object_ref(priv->udev_device);
	while (udev_device != NULL) {
		g_autoptr(GUdevDevice) udev_device_parent = NULL;
		bus = g_udev_device_get_sysfs_attr_as_int(udev_device, "busnum");
		address = g_udev_device_get_sysfs_attr_as_int(udev_device, "devnum");
		if (bus != 0 || address != 0)
			break;
		udev_device_parent = g_udev_device_get_parent(udev_device);
		g_set_object(&udev_device, udev_device_parent);
	}

	/* nothing found */
	if (bus == 0x0 && address == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "No parent device with busnum and devnum");
		return NULL;
	}

	/* match device */
	usb_ctx = g_usb_context_new(error);
	if (usb_ctx == NULL)
		return NULL;
	usb_device = g_usb_context_find_by_bus_address(usb_ctx, bus, address, error);
	if (usb_device == NULL)
		return NULL;
	g_usb_device_add_tag(usb_device, "is-transient");
	return FU_DEVICE(fu_usb_device_new(fu_device_get_context(FU_DEVICE(self)), usb_device));
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as <gudev.h> or <gusb.h> is unavailable");
	return NULL;
#endif
}

static GBytes *
fu_udev_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	guint number_reads = 0;
	g_autofree gchar *fn = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* open the file */
	if (priv->device_file == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Unable to read firmware from device");
		return NULL;
	}

	/* open file */
	file = g_file_new_for_path(priv->device_file);
	stream = G_INPUT_STREAM(g_file_read(file, NULL, &error_local));
	if (stream == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AUTH_FAILED,
				    error_local->message);
		return NULL;
	}

	/* we have to enable the read for devices */
	fn = g_file_get_path(file);
	if (g_str_has_prefix(fn, "/sys")) {
		g_autoptr(GFileOutputStream) output_stream = NULL;
		output_stream = g_file_replace(file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error);
		if (output_stream == NULL) {
			fu_error_convert(error);
			return NULL;
		}
		if (!g_output_stream_write_all(G_OUTPUT_STREAM(output_stream),
					       "1",
					       1,
					       NULL,
					       NULL,
					       error)) {
			fu_error_convert(error);
			return NULL;
		}
	}

	/* ensure we got enough data to fill the buffer */
	while (TRUE) {
		gssize sz;
		guint8 tmp[32 * 1024] = {0x0};
		sz = g_input_stream_read(stream, tmp, sizeof(tmp), NULL, error);
		if (sz == 0)
			break;
		g_debug("ROM returned 0x%04x bytes", (guint)sz);
		if (sz < 0)
			return NULL;
		g_byte_array_append(buf, tmp, sz);

		/* check the firmware isn't serving us small chunks */
		if (number_reads++ > 1024) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "firmware not fulfilling requests");
			return NULL;
		}
	}
	return g_bytes_new(buf->data, buf->len);
}

static void
fu_udev_device_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuDevice *device = FU_DEVICE(codec);
	FuUdevDevice *self = FU_UDEV_DEVICE(codec);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	GPtrArray *events = fu_device_get_events(device);

	/* optional properties */
	if (fu_udev_device_get_sysfs_path(self) != NULL)
		fwupd_codec_json_append(builder, "BackendId", fu_udev_device_get_sysfs_path(self));
	if (priv->device_file != NULL)
		fwupd_codec_json_append(builder, "DeviceFile", priv->device_file);
	if (priv->subsystem != NULL)
		fwupd_codec_json_append(builder, "Subsystem", priv->subsystem);
	if (priv->driver != NULL)
		fwupd_codec_json_append(builder, "Driver", priv->driver);
	if (priv->bind_id != NULL)
		fwupd_codec_json_append(builder, "BindId", priv->bind_id);
	if (priv->vendor != 0)
		fwupd_codec_json_append_int(builder, "Vendor", priv->vendor);
	if (priv->model != 0)
		fwupd_codec_json_append_int(builder, "Model", priv->model);
	if (priv->subsystem_vendor != 0)
		fwupd_codec_json_append_int(builder, "SubsystemVendor", priv->subsystem_vendor);
	if (priv->subsystem_model != 0)
		fwupd_codec_json_append_int(builder, "SubsystemModel", priv->subsystem_model);
	if (priv->class != 0)
		fwupd_codec_json_append_int(builder, "PciClass", priv->class);
	if (priv->revision != 0)
		fwupd_codec_json_append_int(builder, "Revision", priv->revision);
	if (priv->number != 0)
		fwupd_codec_json_append_int(builder, "Number", priv->number);

#if GLIB_CHECK_VERSION(2, 62, 0)
	if (fu_device_get_created(device) != 0) {
		g_autoptr(GDateTime) dt =
		    g_date_time_new_from_unix_utc(fu_device_get_created(device));
		g_autofree gchar *str = g_date_time_format_iso8601(dt);
		json_builder_set_member_name(builder, "Created");
		json_builder_add_string_value(builder, str);
	}
#endif

	/* events */
	if (events->len > 0) {
		json_builder_set_member_name(builder, "Events");
		json_builder_begin_array(builder);
		for (guint i = 0; i < events->len; i++) {
			FuDeviceEvent *event = g_ptr_array_index(events, i);
			fwupd_codec_to_json(FWUPD_CODEC(event), builder, flags);
		}
		json_builder_end_array(builder);
	}
}

static gboolean
fu_udev_device_from_json(FwupdCodec *codec, JsonNode *json_node, GError **error)
{
	FuDevice *device = FU_DEVICE(codec);
	FuUdevDevice *self = FU_UDEV_DEVICE(codec);
	JsonObject *json_object = json_node_get_object(json_node);
	const gchar *tmp;
	gint64 tmp64;

	tmp = json_object_get_string_member_with_default(json_object, "BackendId", NULL);
	if (tmp != NULL)
		fu_device_set_backend_id(device, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "Subsystem", NULL);
	if (tmp != NULL)
		fu_udev_device_set_subsystem(self, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "Driver", NULL);
	if (tmp != NULL)
		fu_udev_device_set_driver(self, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "BindId", NULL);
	if (tmp != NULL)
		fu_udev_device_set_bind_id(self, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "DeviceFile", NULL);
	if (tmp != NULL)
		fu_udev_device_set_device_file(self, tmp);
	tmp64 = json_object_get_int_member_with_default(json_object, "Vendor", 0);
	if (tmp64 != 0)
		fu_udev_device_set_vendor(self, tmp64);
	tmp64 = json_object_get_int_member_with_default(json_object, "Model", 0);
	if (tmp64 != 0)
		fu_udev_device_set_model(self, tmp64);
	tmp64 = json_object_get_int_member_with_default(json_object, "SubsystemVendor", 0);
	if (tmp64 != 0)
		fu_udev_device_set_subsystem_vendor(self, tmp64);
	tmp64 = json_object_get_int_member_with_default(json_object, "SubsystemModel", 0);
	if (tmp64 != 0)
		fu_udev_device_set_subsystem_model(self, tmp64);
	tmp64 = json_object_get_int_member_with_default(json_object, "PciClass", 0);
	if (tmp64 != 0)
		fu_udev_device_set_cls(self, tmp64);
	tmp64 = json_object_get_int_member_with_default(json_object, "Revision", 0);
	if (tmp64 != 0)
		fu_udev_device_set_revision(self, tmp64);
	tmp64 = json_object_get_int_member_with_default(json_object, "Number", 0);
	if (tmp64 != 0)
		fu_udev_device_set_number(self, tmp64);

	tmp = json_object_get_string_member_with_default(json_object, "Created", NULL);
	if (tmp != NULL) {
		g_autoptr(GDateTime) dt = g_date_time_new_from_iso8601(tmp, NULL);
		if (dt != NULL)
			fu_device_set_created(device, g_date_time_to_unix(dt));
	}

	/* array of events */
	if (json_object_has_member(json_object, "Events")) {
		JsonArray *json_array = json_object_get_array_member(json_object, "Events");
		for (guint i = 0; i < json_array_get_length(json_array); i++) {
			JsonNode *node_tmp = json_array_get_element(json_array, i);
			g_autoptr(FuDeviceEvent) event = fu_device_event_new(NULL);
			if (!fwupd_codec_from_json(FWUPD_CODEC(event), node_tmp, error))
				return FALSE;
			fu_device_add_event(device, event);
		}
	}

	/* success */
	return TRUE;
}

static void
fu_udev_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(object);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_UDEV_DEVICE:
		g_value_set_object(value, priv->udev_device);
		break;
	case PROP_SUBSYSTEM:
		g_value_set_string(value, priv->subsystem);
		break;
	case PROP_BIND_ID:
		g_value_set_string(value, priv->bind_id);
		break;
	case PROP_DRIVER:
		g_value_set_string(value, priv->driver);
		break;
	case PROP_DEVICE_FILE:
		g_value_set_string(value, priv->device_file);
		break;
	case PROP_DEVTYPE:
		g_value_set_string(value, priv->devtype);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_udev_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(object);
	switch (prop_id) {
	case PROP_UDEV_DEVICE:
		fu_udev_device_set_dev(self, g_value_get_object(value));
		break;
	case PROP_SUBSYSTEM:
		fu_udev_device_set_subsystem(self, g_value_get_string(value));
		break;
	case PROP_BIND_ID:
		fu_udev_device_set_bind_id(self, g_value_get_string(value));
		break;
	case PROP_DRIVER:
		fu_udev_device_set_driver(self, g_value_get_string(value));
		break;
	case PROP_DEVICE_FILE:
		fu_udev_device_set_device_file(self, g_value_get_string(value));
		break;
	case PROP_DEVTYPE:
		fu_udev_device_set_devtype(self, g_value_get_string(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_udev_device_finalize(GObject *object)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(object);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	g_free(priv->subsystem);
	g_free(priv->devtype);
	g_free(priv->bind_id);
	g_free(priv->driver);
	g_free(priv->device_file);
	if (priv->udev_device != NULL)
		g_object_unref(priv->udev_device);
	if (priv->io_channel != NULL)
		g_object_unref(priv->io_channel);

	G_OBJECT_CLASS(fu_udev_device_parent_class)->finalize(object);
}

static void
fu_udev_device_init(FuUdevDevice *self)
{
	fu_device_set_acquiesce_delay(FU_DEVICE(self), 2500);
}

static void
fu_udev_device_class_init(FuUdevDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fu_udev_device_finalize;
	object_class->get_property = fu_udev_device_get_property;
	object_class->set_property = fu_udev_device_set_property;
	device_class->probe = fu_udev_device_probe;
	device_class->rescan = fu_udev_device_rescan;
	device_class->incorporate = fu_udev_device_incorporate;
	device_class->open = fu_udev_device_open;
	device_class->close = fu_udev_device_close;
	device_class->to_string = fu_udev_device_to_string;
	device_class->bind_driver = fu_udev_device_bind_driver;
	device_class->unbind_driver = fu_udev_device_unbind_driver;
	device_class->probe_complete = fu_udev_device_probe_complete;
	device_class->dump_firmware = fu_udev_device_dump_firmware;

	/**
	 * FuUdevDevice::changed:
	 * @self: the #FuUdevDevice instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the low-level GUdevDevice has changed.
	 *
	 * Since: 1.1.2
	 **/
	signals[SIGNAL_CHANGED] = g_signal_new("changed",
					       G_TYPE_FROM_CLASS(object_class),
					       G_SIGNAL_RUN_LAST,
					       0,
					       NULL,
					       NULL,
					       g_cclosure_marshal_VOID__VOID,
					       G_TYPE_NONE,
					       0);

	/**
	 * FuUdevDevice:udev-device:
	 *
	 * The low-level GUdevDevice.
	 *
	 * Since: 1.1.2
	 */
	pspec = g_param_spec_object("udev-device",
				    NULL,
				    NULL,
				    G_UDEV_TYPE_DEVICE,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_UDEV_DEVICE, pspec);

	/**
	 * FuUdevDevice:subsystem:
	 *
	 * The device subsystem.
	 *
	 * Since: 1.1.2
	 */
	pspec = g_param_spec_string("subsystem",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_SUBSYSTEM, pspec);

	/**
	 * FuUdevDevice:bind-id:
	 *
	 * The bind ID to use when binding a new driver.
	 *
	 * Since: 1.7.2
	 */
	pspec = g_param_spec_string("bind-id",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_BIND_ID, pspec);

	/**
	 * FuUdevDevice:driver:
	 *
	 * The driver being used for the device.
	 *
	 * Since: 1.5.3
	 */
	pspec = g_param_spec_string("driver",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_DRIVER, pspec);

	/**
	 * FuUdevDevice:device-file:
	 *
	 * The low level file to use for device access.
	 *
	 * Since: 1.3.1
	 */
	pspec = g_param_spec_string("device-file",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_DEVICE_FILE, pspec);

	/**
	 * FuUdevDevice:devtype:
	 *
	 * The device type.
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_string("devtype",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_DEVTYPE, pspec);
}

static void
fu_udev_device_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_udev_device_add_json;
	iface->from_json = fu_udev_device_from_json;
}

/**
 * fu_udev_device_new:
 * @ctx: (nullable): a #FuContext
 * @udev_device: a #GUdevDevice
 *
 * Creates a new #FuUdevDevice.
 *
 * Returns: (transfer full): a #FuUdevDevice
 *
 * Since: 1.8.2
 **/
FuUdevDevice *
fu_udev_device_new(FuContext *ctx, GUdevDevice *udev_device)
{
	return g_object_new(FU_TYPE_UDEV_DEVICE, "context", ctx, "udev-device", udev_device, NULL);
}
