/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuUdevDevice"

#include "config.h"

#include <fcntl.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_IOCTL_H
#include <sys/ioctl.h>
#endif
#include <glib/gstdio.h>

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
	FuIOChannelOpenFlags open_flags;
	gchar **uevent_lines;
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

static void
fu_udev_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *open_flags = fu_io_channel_open_flags_to_string(priv->open_flags);

	fwupd_codec_string_append_hex(str, idt, "Vendor", priv->vendor);
	fwupd_codec_string_append_hex(str, idt, "Model", priv->model);
	fwupd_codec_string_append_hex(str, idt, "SubsystemVendor", priv->subsystem_vendor);
	fwupd_codec_string_append_hex(str, idt, "SubsystemModel", priv->subsystem_model);
	fwupd_codec_string_append_hex(str, idt, "Class", priv->class);
	fwupd_codec_string_append_hex(str, idt, "Revision", priv->revision);
	fwupd_codec_string_append_hex(str, idt, "Number", priv->number);
	fwupd_codec_string_append(str, idt, "Subsystem", priv->subsystem);
	fwupd_codec_string_append(str, idt, "Driver", priv->driver);
	fwupd_codec_string_append(str, idt, "BindId", priv->bind_id);
	fwupd_codec_string_append(str, idt, "DeviceFile", priv->device_file);
	fwupd_codec_string_append(str, idt, "OpenFlags", open_flags);
}

static gboolean
fu_udev_device_ensure_bind_id(FuUdevDevice *self, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* sanity check */
	if (priv->bind_id != NULL)
		return TRUE;

	/* automatically set the bind ID from the subsystem */
	if (g_strcmp0(priv->subsystem, "pci") == 0) {
		priv->bind_id = fu_udev_device_read_property(self, "PCI_SLOT_NAME", error);
		return priv->bind_id != NULL;
	}
	if (g_strcmp0(priv->subsystem, "hid") == 0) {
		priv->bind_id = fu_udev_device_read_property(self, "HID_PHYS", error);
		return priv->bind_id != NULL;
	}
	if (g_strcmp0(priv->subsystem, "usb") == 0) {
		priv->bind_id = g_path_get_basename(fu_udev_device_get_sysfs_path(self));
		return TRUE;
	}

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

static gchar *
_fu_path_get_symlink_basename(const gchar *dirname, const gchar *basename, GError **error)
{
	const gchar *target;
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	fn = g_build_filename(dirname, basename, NULL);
	file = g_file_new_for_path(fn);
	info = g_file_query_info(file,
				 G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET,
				 G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
				 NULL,
				 error);
	if (info == NULL) {
		fu_error_convert(error);
		return NULL;
	}
	target =
	    g_file_info_get_attribute_byte_string(info, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET);
	if (target == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no symlink target");
		return NULL;
	}

	/* success */
	return g_path_get_basename(target);
}

static gchar *
fu_udev_device_get_symlink_target(FuUdevDevice *self, const gchar *attr, GError **error)
{
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;
	g_autofree gchar *value = NULL;

	if (fu_udev_device_get_sysfs_path(self) == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "not initialized");
		return NULL;
	}

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("GetSymlinkTarget:Attr=%s", attr);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		return g_strdup(fu_device_event_get_str(event, "Data", error));
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	/* find target */
	value = _fu_path_get_symlink_basename(fu_udev_device_get_sysfs_path(self), attr, error);
	if (value == NULL)
		return NULL;

	/* save response */
	if (event != NULL)
		fu_device_event_set_str(event, "Data", value);

	/* success */
	return g_steal_pointer(&value);
}

/**
 * fu_udev_device_set_vendor:
 * @self: a #FuUdevDevice
 * @vendor: an ID
 *
 * Sets the vendor ID.
 *
 * Since: 2.0.0
 **/
void
fu_udev_device_set_vendor(FuUdevDevice *self, guint16 vendor)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	priv->vendor = vendor;
}

/**
 * fu_udev_device_set_model:
 * @self: a #FuUdevDevice
 * @model: an ID
 *
 * Sets the model ID.
 *
 * Since: 2.0.0
 **/
void
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

static gchar *
fu_udev_device_get_miscdev0(FuUdevDevice *self)
{
	const gchar *fn;
	g_autofree gchar *miscdir = NULL;
	g_autoptr(GDir) dir = NULL;

	miscdir = g_build_filename(fu_udev_device_get_sysfs_path(self), "misc", NULL);
	dir = g_dir_open(miscdir, 0, NULL);
	if (dir == NULL)
		return NULL;
	fn = g_dir_read_name(dir);
	if (fn == NULL)
		return NULL;
	return g_strdup_printf("/dev/%s", fn);
}

static gboolean
fu_udev_device_probe_serio(FuUdevDevice *self, FuUdevDevice *device_parent, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *attr_description = NULL;
	g_autofree gchar *prop_serio_id = NULL;

	/* firmware ID */
	prop_serio_id = fu_udev_device_read_property(device_parent, "SERIO_FIRMWARE_ID", NULL);
	if (prop_serio_id != NULL) {
		g_autofree gchar *prop_serio_id_noprefix = NULL;
		/* this prefix is not useful */
		if (g_str_has_prefix(prop_serio_id, "PNP: ")) {
			prop_serio_id_noprefix = g_ascii_strup(prop_serio_id + 5, -1);
		} else {
			prop_serio_id_noprefix = g_ascii_strup(prop_serio_id, -1);
		}
		fu_device_add_instance_strsafe(FU_DEVICE(self), "FWID", prop_serio_id_noprefix);
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

	/* description */
	attr_description = fu_udev_device_read_sysfs(self,
						     "description",
						     FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						     NULL);
	if (attr_description != NULL)
		fu_device_set_summary(FU_DEVICE(self), attr_description);

	/* fall back to the first thing handled by misc drivers */
	if (priv->device_file == NULL)
		priv->device_file = fu_udev_device_get_miscdev0(self);

	/* success */
	return TRUE;
}

static gboolean
fu_udev_device_probe_hid(FuUdevDevice *self, FuUdevDevice *device_parent, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *prop_id = NULL;
	g_auto(GStrv) split = NULL;

	/* ID */
	prop_id = fu_udev_device_read_property(device_parent, "HID_ID", error);
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
			fu_udev_device_set_vendor(self, (guint16)val);
		}
		if (priv->model == 0x0) {
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
			fu_udev_device_set_model(self, (guint16)val);
		}
	}

	/* set name */
	if (fu_device_get_name(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *prop_name =
		    fu_udev_device_read_property(device_parent, "HID_NAME", NULL);
		if (prop_name != NULL)
			fu_device_set_name(FU_DEVICE(self), prop_name);
	}

	/* set the logical ID */
	if (fu_device_get_logical_id(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *logical_id =
		    fu_udev_device_read_property(device_parent, "HID_UNIQ", NULL);
		if (logical_id != NULL)
			fu_device_set_logical_id(FU_DEVICE(self), logical_id);
	}

	/* set the physical ID */
	if (fu_device_get_physical_id(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *physical_id = NULL;
		physical_id = fu_udev_device_read_property(device_parent, "HID_PHYS", error);
		if (physical_id == NULL)
			return FALSE;
		fu_device_set_physical_id(FU_DEVICE(self), physical_id);
	}

	/* USB\\VID_1234 */
	fu_device_add_instance_u16(FU_DEVICE(self), "VID", priv->vendor);
	return TRUE;
}

static gboolean
fu_udev_device_probe_usb(FuUdevDevice *self, FuUdevDevice *device_parent, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	guint64 tmp64 = 0;

	/* idVendor=093a */
	if (fu_device_get_vendor(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *vendor_str = NULL;
		vendor_str = fu_udev_device_read_sysfs(device_parent,
						       "idVendor",
						       FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						       error);
		if (vendor_str == NULL)
			return FALSE;
		if (!fu_strtoull(vendor_str, &tmp64, 0x0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
			return FALSE;
		priv->vendor = (guint16)tmp64;
	}

	/* idProduct=2862 */
	if (priv->model == 0x0) {
		g_autofree gchar *model_str = NULL;
		model_str = fu_udev_device_read_sysfs(device_parent,
						      "idProduct",
						      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						      error);
		if (model_str == NULL)
			return FALSE;
		if (!fu_strtoull(model_str, &tmp64, 0x0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
			return FALSE;
		priv->model = (guint16)tmp64;
	}

	/* bcdDevice=0000 */
	if (priv->revision == 0x0) {
		g_autofree gchar *revision_str = NULL;
		revision_str = fu_udev_device_read_sysfs(device_parent,
							 "bcdDevice",
							 FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
							 error);
		if (revision_str == NULL)
			return FALSE;
		if (!fu_strtoull(revision_str, &tmp64, 0x0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
			return FALSE;
		priv->revision = (guint16)tmp64;
	}

	/* bDeviceClass=09 */
	if (priv->class == 0x0) {
		g_autofree gchar *pci_class_str = NULL;
		pci_class_str = fu_udev_device_read_sysfs(device_parent,
							  "bDeviceClass",
							  FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
							  error);
		if (pci_class_str == NULL)
			return FALSE;
		if (!fu_strtoull(pci_class_str, &tmp64, 0x0, G_MAXUINT8, FU_INTEGER_BASE_16, error))
			return FALSE;
		priv->class = (guint16)tmp64;
	}

	/* set the physical ID */
	if (fu_device_get_physical_id(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *physical_id = NULL;
		physical_id = fu_udev_device_read_property(device_parent, "DEVNAME", error);
		if (physical_id == NULL)
			return FALSE;
		fu_device_set_physical_id(FU_DEVICE(self), physical_id);
	}

	/* USB\\VID_1234 */
	fu_device_add_instance_u16(FU_DEVICE(self), "VID", priv->vendor);
	return TRUE;
}

static gboolean
fu_udev_device_probe_pci(FuUdevDevice *self, FuUdevDevice *device_parent, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	guint64 tmp64 = 0;

	/* vendor=0x8086 */
	if (fu_device_get_vendor(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *subsystem_vendor_str = NULL;
		g_autofree gchar *vendor_str = NULL;
		vendor_str = fu_udev_device_read_sysfs(device_parent,
						       "vendor",
						       FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						       error);
		if (vendor_str == NULL)
			return FALSE;
		if (!fu_strtoull(vendor_str, &tmp64, 0x0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
			return FALSE;
		priv->vendor = (guint16)tmp64;

		/* subsystem_vendor=0x8086 */
		subsystem_vendor_str =
		    fu_udev_device_read_sysfs(device_parent,
					      "subsystem_vendor",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      error);
		if (subsystem_vendor_str == NULL)
			return FALSE;
		if (!fu_strtoull(subsystem_vendor_str,
				 &tmp64,
				 0x0,
				 G_MAXUINT16,
				 FU_INTEGER_BASE_16,
				 error))
			return FALSE;
		priv->subsystem_vendor = (guint16)tmp64;
	}

	/* device=0x06ed */
	if (priv->model == 0x0) {
		g_autofree gchar *model_str = NULL;
		g_autofree gchar *subsystem_model_str = NULL;
		model_str = fu_udev_device_read_sysfs(device_parent,
						      "device",
						      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						      error);
		if (model_str == NULL)
			return FALSE;
		if (!fu_strtoull(model_str, &tmp64, 0x0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
			return FALSE;
		priv->model = (guint16)tmp64;

		/* subsystem_device=0x06ed */
		subsystem_model_str =
		    fu_udev_device_read_sysfs(device_parent,
					      "subsystem_device",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      error);
		if (subsystem_model_str == NULL)
			return FALSE;
		if (!fu_strtoull(subsystem_model_str,
				 &tmp64,
				 0x0,
				 G_MAXUINT16,
				 FU_INTEGER_BASE_16,
				 error))
			return FALSE;
		priv->subsystem_model = (guint16)tmp64;
	}

	/* revision=0x00 */
	if (priv->revision == 0x0) {
		g_autofree gchar *revision_str = NULL;
		revision_str = fu_udev_device_read_sysfs(device_parent,
							 "revision",
							 FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
							 error);
		if (revision_str == NULL)
			return FALSE;
		if (!fu_strtoull(revision_str, &tmp64, 0x0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
			return FALSE;
		priv->revision = (guint16)tmp64;
	}

	/* class=0x0c0330 */
	if (priv->class == 0x0) {
		g_autofree gchar *pci_class_str = NULL;
		pci_class_str = fu_udev_device_read_sysfs(device_parent,
							  "class",
							  FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
							  error);
		if (pci_class_str == NULL)
			return FALSE;
		if (!fu_strtoull(pci_class_str,
				 &tmp64,
				 0x0,
				 G_MAXUINT32,
				 FU_INTEGER_BASE_16,
				 error))
			return FALSE;
		priv->class = (guint32)tmp64;

		/* the things we do to avoid changing instance IDs... */
		if (pci_class_str != NULL && g_str_has_prefix(pci_class_str, "0x")) {
			fu_device_add_instance_strup(FU_DEVICE(self), "CLASS", pci_class_str + 2);
		} else {
			fu_device_add_instance_strup(FU_DEVICE(self), "CLASS", pci_class_str);
		}
	}

	/* if the device is a GPU try to fetch it from vbios_version */
	if (fu_udev_device_is_pci_base_cls(self, FU_PCI_BASE_CLS_DISPLAY) &&
	    fu_device_get_version(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *attr_version = NULL;
		attr_version = fu_udev_device_read_property(self, "vbios_version", NULL);
		if (attr_version != NULL) {
			fu_device_set_version(FU_DEVICE(self), attr_version);
			fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_add_icon(FU_DEVICE(self), "video-display");
		}
	}

	/* set the physical ID */
	if (fu_device_get_physical_id(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *prop_slot_name = NULL;
		g_autofree gchar *physical_id = NULL;
		prop_slot_name =
		    fu_udev_device_read_property(device_parent, "PCI_SLOT_NAME", error);
		if (prop_slot_name == NULL)
			return FALSE;
		physical_id = g_strdup_printf("PCI_SLOT_NAME=%s", prop_slot_name);
		fu_device_set_physical_id(FU_DEVICE(self), physical_id);
	}

	/* PCI\\VEN_1234 */
	fu_device_add_instance_u16(FU_DEVICE(self), "VEN", priv->vendor);
	return TRUE;
}

static gboolean
fu_udev_device_probe_scsi(FuUdevDevice *self, FuUdevDevice *device_parent, GError **error)
{
	/* vendor and model as attributes */
	if (fu_device_get_vendor(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *attr_vendor = NULL;
		attr_vendor = fu_udev_device_read_sysfs(device_parent,
							"vendor",
							FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
							NULL);
		if (attr_vendor != NULL)
			fu_device_set_vendor(FU_DEVICE(self), attr_vendor);
	}
	if (fu_device_get_name(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *attr_model = NULL;
		attr_model = fu_udev_device_read_sysfs(device_parent,
						       "model",
						       FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						       NULL);
		if (attr_model != NULL)
			fu_device_set_name(FU_DEVICE(self), attr_model);
	}

	/* set the physical ID */
	if (fu_device_get_physical_id(FU_DEVICE(self)) == NULL) {
		g_autofree gchar *physical_id = NULL;
		g_autofree gchar *basename =
		    g_path_get_basename(fu_udev_device_get_sysfs_path(device_parent));
		physical_id = g_strdup_printf("DEVPATH=%s", basename);
		fu_device_set_physical_id(FU_DEVICE(self), physical_id);
	}

	/* success */
	return TRUE;
}

/* private */
gboolean
fu_udev_device_parse_number(FuUdevDevice *self, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GString) path = g_string_new(fu_udev_device_get_sysfs_path(self));

	if (path->len == 0)
		return TRUE;
	for (guint i = path->len - 1; i > 0; i--) {
		if (!g_ascii_isdigit(path->str[i])) {
			g_string_erase(path, 0, i + 1);
			break;
		}
	}
	if (path->len > 0) {
		if (!fu_strtoull(path->str,
				 &priv->number,
				 0x0,
				 G_MAXUINT64,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static FuUdevDevice *
fu_udev_device_new_from_sysfs_path_match_subsystem_devtype(FuContext *ctx,
							   const gchar *dirname,
							   const gchar *subsystem,
							   const gchar *devtype)
{
	g_autofree gchar *subsystem_tmp = NULL;
	g_autoptr(FuUdevDevice) device_tmp = NULL;

	/* check subsystem matches if specified */
	subsystem_tmp = _fu_path_get_symlink_basename(dirname, "subsystem", NULL);
	if (subsystem_tmp == NULL)
		return NULL;
	if (subsystem != NULL && g_strcmp0(subsystem_tmp, subsystem) != 0)
		return NULL;

	/* check devtype matches, if specified */
	device_tmp = fu_udev_device_new_from_sysfs_path(ctx, dirname);
	if (devtype != NULL) {
		g_autofree gchar *devtype_tmp = NULL;
		devtype_tmp = fu_udev_device_read_property(device_tmp, "DEVTYPE", NULL);
		if (devtype_tmp == NULL)
			return NULL;
		if (g_strcmp0(devtype, devtype_tmp) != 0)
			return NULL;
		fu_udev_device_set_devtype(device_tmp, devtype);
	}

	/* success */
	fu_udev_device_set_subsystem(device_tmp, subsystem_tmp);
	return g_steal_pointer(&device_tmp);
}

static FuUdevDevice *
fu_udev_device_get_parent_with_subsystem_internal(FuUdevDevice *self,
						  const gchar *subsystem,
						  const gchar *devtype,
						  GError **error)
{
	g_autofree gchar *devtype_new = NULL;
	g_autofree gchar *sysfs_path = NULL;
	g_autoptr(FuUdevDevice) new = NULL;

	sysfs_path = g_strdup(fu_udev_device_get_sysfs_path(self));
	if (sysfs_path == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "sysfs_path undefined");
		return NULL;
	}

	/* lets just walk up the directories */
	while (1) {
		g_autofree gchar *dirname = NULL;

		/* done? */
		dirname = g_path_get_dirname(sysfs_path);
		if (g_strcmp0(dirname, ".") == 0 || g_strcmp0(dirname, "/") == 0)
			break;

		/* check has matching subsystem and devtype */
		new = fu_udev_device_new_from_sysfs_path_match_subsystem_devtype(
		    fu_device_get_context(FU_DEVICE(self)),
		    dirname,
		    subsystem,
		    devtype);
		if (new != NULL)
			break;

		/* just swap, and go deeper */
		g_free(sysfs_path);
		sysfs_path = g_steal_pointer(&dirname);
	}

	/* failed */
	if (new == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "no parent with subsystem %s",
			    subsystem);
		return NULL;
	}

	/* optimize slightly by setting devtype early */
	devtype_new = fu_udev_device_read_property(new, "DEVTYPE", NULL);
	if (devtype_new != NULL)
		fu_udev_device_set_devtype(new, devtype_new);

	/* success */
	return g_steal_pointer(&new);
}

typedef enum {
	FU_UDEV_DEVICE_PROBE_FLAG_NONE = 0,
	FU_UDEV_DEVICE_PROBE_FLAG_HID = 1 << 0,
	FU_UDEV_DEVICE_PROBE_FLAG_USB = 1 << 1,
	FU_UDEV_DEVICE_PROBE_FLAG_PCI = 1 << 2,
	FU_UDEV_DEVICE_PROBE_FLAG_SERIO = 1 << 3,
	FU_UDEV_DEVICE_PROBE_FLAG_SCSI = 1 << 4,
	FU_UDEV_DEVICE_PROBE_FLAG_NUMBER = 1 << 5,
} FuUdevDeviceProbeFlags;

static gboolean
fu_udev_device_probe_for_subsystem(FuUdevDevice *self,
				   FuUdevDeviceProbeFlags probe_flags,
				   GError **error)
{
	g_autoptr(FuUdevDevice) device_parent = g_object_ref(self);

	/* go up until we find something */
	while (1) {
		g_autoptr(FuUdevDevice) device_donor = NULL;

		/* probe HID properties */
		if (probe_flags & FU_UDEV_DEVICE_PROBE_FLAG_HID &&
		    g_strcmp0(fu_udev_device_get_subsystem(device_parent), "hid") == 0) {
			return fu_udev_device_probe_hid(self, device_parent, error);
		}

		/* probe USB properties */
		if (probe_flags & FU_UDEV_DEVICE_PROBE_FLAG_USB &&
		    g_strcmp0(fu_udev_device_get_subsystem(device_parent), "usb") == 0 &&
		    g_strcmp0(fu_udev_device_get_devtype(device_parent), "usb_device") == 0) {
			return fu_udev_device_probe_usb(self, device_parent, error);
		}

		/* probe SCSI properties */
		if (probe_flags & FU_UDEV_DEVICE_PROBE_FLAG_SCSI &&
		    g_strcmp0(fu_udev_device_get_subsystem(device_parent), "scsi") == 0) {
			return fu_udev_device_probe_scsi(self, device_parent, error);
		}

		/* probe PCI properties */
		if (probe_flags & FU_UDEV_DEVICE_PROBE_FLAG_PCI &&
		    g_strcmp0(fu_udev_device_get_subsystem(device_parent), "pci") == 0) {
			return fu_udev_device_probe_pci(self, device_parent, error);
		}

		/* probe serial properties */
		if (probe_flags & FU_UDEV_DEVICE_PROBE_FLAG_SERIO &&
		    g_strcmp0(fu_udev_device_get_subsystem(device_parent), "serio") == 0) {
			return fu_udev_device_probe_serio(self, device_parent, error);
		}

		/* go up */
		device_donor = fu_udev_device_get_parent_with_subsystem_internal(device_parent,
										 NULL,
										 NULL,
										 NULL);
		if (device_donor == NULL)
			break;
		g_set_object(&device_parent, device_donor);
	}

	/* failed */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "failed to get subsystem parent for %s:%s",
		    fu_udev_device_get_subsystem(self),
		    fu_udev_device_get_devtype(self));
	return FALSE;
}

static gboolean
fu_udev_device_probe(FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *subsystem = NULL;
	g_autofree gchar *subsystem_strup = NULL;
	g_autoptr(FuUdevDevice) device_i2c = NULL;
	FuUdevDeviceProbeFlags probe_flags = FU_UDEV_DEVICE_PROBE_FLAG_NONE;
	struct {
		const gchar *subsystem;
		FuUdevDeviceProbeFlags probe_flags;
	} probe_map[] = {
	    {"block",
	     FU_UDEV_DEVICE_PROBE_FLAG_PCI | FU_UDEV_DEVICE_PROBE_FLAG_SCSI |
		 FU_UDEV_DEVICE_PROBE_FLAG_NUMBER},
	    {"hid", FU_UDEV_DEVICE_PROBE_FLAG_HID | FU_UDEV_DEVICE_PROBE_FLAG_NUMBER},
	    {"hidraw", FU_UDEV_DEVICE_PROBE_FLAG_HID},
	    {"i2c", FU_UDEV_DEVICE_PROBE_FLAG_PCI},
	    {"mei", FU_UDEV_DEVICE_PROBE_FLAG_PCI},
	    {"mtd", FU_UDEV_DEVICE_PROBE_FLAG_PCI},
	    {"net", FU_UDEV_DEVICE_PROBE_FLAG_PCI | FU_UDEV_DEVICE_PROBE_FLAG_NUMBER},
	    {"nvme", FU_UDEV_DEVICE_PROBE_FLAG_PCI | FU_UDEV_DEVICE_PROBE_FLAG_NUMBER},
	    {"pci", FU_UDEV_DEVICE_PROBE_FLAG_PCI},
	    {"platform", FU_UDEV_DEVICE_PROBE_FLAG_PCI},
	    {"serio", FU_UDEV_DEVICE_PROBE_FLAG_PCI | FU_UDEV_DEVICE_PROBE_FLAG_SERIO},
	    {"usb", FU_UDEV_DEVICE_PROBE_FLAG_USB},
	    {"video4linux", FU_UDEV_DEVICE_PROBE_FLAG_USB | FU_UDEV_DEVICE_PROBE_FLAG_PCI},
	};

	/* find the subsystem */
	if (priv->subsystem == NULL) {
		subsystem = fu_udev_device_get_symlink_target(self, "subsystem", error);
		if (subsystem == NULL)
			return FALSE;
		fu_udev_device_set_subsystem(self, subsystem);
	} else {
		subsystem = g_strdup(priv->subsystem);
	}

	if (priv->driver == NULL)
		priv->driver = fu_udev_device_get_symlink_target(self, "driver", NULL);
	if (priv->devtype == NULL)
		priv->devtype = fu_udev_device_read_property(self, "DEVTYPE", NULL);

	/* add possible parent subsystems for each child subsystem */
	for (guint i = 0; i < G_N_ELEMENTS(probe_map); i++) {
		if (g_strcmp0(subsystem, probe_map[i].subsystem) == 0) {
			probe_flags = probe_map[i].probe_flags;
			break;
		}
	}
	if (probe_flags != FU_UDEV_DEVICE_PROBE_FLAG_NONE) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_udev_device_probe_for_subsystem(self, probe_flags, &error_local))
			g_debug("ignoring: %s", error_local->message);
	}

	/* fall back to DEVNAME */
	if (fu_udev_device_get_device_file(self) == NULL) {
		g_autofree gchar *prop_devname =
		    fu_udev_device_read_property(self, "DEVNAME", NULL);
		g_autofree gchar *device_file = g_strdup_printf("/dev/%s", prop_devname);
		if (prop_devname != NULL)
			fu_udev_device_set_device_file(self, device_file);
	}
	if (fu_device_get_physical_id(device) == NULL && priv->device_file != NULL) {
		g_autofree gchar *physical_id = g_strdup_printf("DEVNAME=%s", priv->device_file);
		fu_device_set_physical_id(device, physical_id);
	}

	/* vendors */
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_GENERIC |
					     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "PCI",
					 "VEN",
					 NULL);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_GENERIC |
					     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "USB",
					 "VID",
					 NULL);

	/* set the version if the revision has been set */
	if (fu_device_get_version(device) == NULL &&
	    fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		if (priv->revision != 0x00 && priv->revision != 0xFF) {
			g_autofree gchar *version = g_strdup_printf("%02x", priv->revision);
			fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_set_version(device, version);
		}
	}

	/* set number */
	if (priv->number == 0 && probe_flags & FU_UDEV_DEVICE_PROBE_FLAG_NUMBER) {
		if (fu_udev_device_get_sysfs_path(self) != NULL) {
			if (!fu_udev_device_parse_number(self, error))
				return FALSE;
		}
	}

	/* set revision */
	if (fu_device_get_version(device) == NULL &&
	    fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		g_autofree gchar *prop_revision = NULL;
		prop_revision = fu_udev_device_read_property(self, "ID_REVISION", NULL);
		if (prop_revision != NULL)
			fu_device_set_version(device, prop_revision);
	}

	/* set vendor ID */
	if (subsystem != NULL)
		subsystem_strup = g_ascii_strup(subsystem, -1);
	if (subsystem_strup != NULL && priv->vendor != 0x0000) {
		g_autofree gchar *vendor_id = NULL;
		vendor_id = g_strdup_printf("%s:0x%04X", subsystem_strup, (guint)priv->vendor);
		fu_device_add_vendor_id(device, vendor_id);
	}

	/* add GUIDs in order of priority */
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
	if (subsystem_strup != NULL) {
		fu_device_add_instance_u16(device, "VEN", priv->vendor);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem_strup,
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
							 subsystem_strup,
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
						 subsystem_strup,
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
							 subsystem_strup,
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
		g_autofree gchar *prop_serial =
		    fu_udev_device_read_property(self, "ID_SERIAL_SHORT", NULL);
		if (prop_serial == NULL)
			prop_serial = fu_udev_device_read_property(self, "ID_SERIAL", NULL);
		if (prop_serial != NULL)
			fu_device_set_serial(device, prop_serial);
	}

	/* add device class */
	if (subsystem_strup != NULL) {
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem_strup,
						 "VEN",
						 "CLASS",
						 NULL);

		/* add devtype */
		fu_device_add_instance_strup(device, "TYPE", priv->devtype);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem_strup,
						 "TYPE",
						 NULL);

		/* add the driver */
		fu_device_add_instance_str(device, "DRIVER", priv->driver);
		fu_device_build_instance_id_full(device,
						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						 NULL,
						 subsystem_strup,
						 "DRIVER",
						 NULL);
	}

	/* determine if we're wired internally */
	device_i2c = fu_udev_device_get_parent_with_subsystem(self, "i2c", NULL, NULL);
	if (device_i2c != NULL)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);

	/* success */
	return TRUE;
}

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

	g_return_if_fail(FU_IS_UDEV_DEVICE(self));

	if (g_set_object(&priv->udev_device, udev_device))
		g_object_notify(G_OBJECT(self), "udev-device");

	/* set new device */
	if (priv->udev_device == NULL)
		return;
#ifdef HAVE_GUDEV
	fu_udev_device_set_subsystem(self, g_udev_device_get_subsystem(priv->udev_device));
	fu_udev_device_set_driver(self, g_udev_device_get_driver(priv->udev_device));
	fu_udev_device_set_device_file(self, g_udev_device_get_device_file(priv->udev_device));
	fu_udev_device_set_devtype(self, g_udev_device_get_devtype(priv->udev_device));
	fu_device_set_backend_id(FU_DEVICE(self), g_udev_device_get_sysfs_path(priv->udev_device));
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
		    fu_udev_device_get_parent_with_subsystem_internal(device_tmp,
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
	g_clear_pointer(&priv->uevent_lines, g_strfreev);
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

	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	g_return_if_fail(FU_IS_UDEV_DEVICE(donor));

	fu_udev_device_set_dev(uself, fu_udev_device_get_dev(udonor));
	if (priv->device_file == NULL)
		fu_udev_device_set_device_file(uself, fu_udev_device_get_device_file(udonor));
	if (priv->subsystem == NULL)
		fu_udev_device_set_subsystem(uself, fu_udev_device_get_subsystem(udonor));
	if (priv->bind_id == NULL)
		fu_udev_device_set_bind_id(uself, fu_udev_device_get_bind_id(udonor));
	if (priv->driver == NULL)
		fu_udev_device_set_driver(uself, fu_udev_device_get_driver(udonor));
	if (priv->devtype == NULL)
		fu_udev_device_set_devtype(uself, fu_udev_device_get_devtype(udonor));
	if (priv->vendor == 0x0)
		fu_udev_device_set_vendor(uself, fu_udev_device_get_vendor(udonor));
	if (priv->model == 0x0)
		fu_udev_device_set_model(uself, fu_udev_device_get_model(udonor));
	if (priv->number == 0x0)
		fu_udev_device_set_number(uself, fu_udev_device_get_number(udonor));
	if (priv->subsystem_vendor == 0x0) {
		fu_udev_device_set_subsystem_vendor(uself,
						    fu_udev_device_get_subsystem_vendor(udonor));
	}
	if (priv->subsystem_model == 0x0) {
		fu_udev_device_set_subsystem_model(uself,
						   fu_udev_device_get_subsystem_model(udonor));
	}
	if (priv->class == 0x0)
		fu_udev_device_set_cls(uself, fu_udev_device_get_cls(udonor));
	if (priv->revision == 0x0)
		fu_udev_device_set_revision(uself, fu_udev_device_get_revision(udonor));
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
 * fu_udev_device_remove_open_flag:
 * @self: a #FuUdevDevice
 * @flag: udev device flag, e.g. %FU_IO_CHANNEL_OPEN_FLAG_READ
 *
 * Removes a open flag.
 *
 * Since: 2.0.0
 **/
void
fu_udev_device_remove_open_flag(FuUdevDevice *self, FuIOChannelOpenFlags flag)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	priv->open_flags &= ~flag;
}

/**
 * fu_udev_device_add_open_flag:
 * @self: a #FuUdevDevice
 * @flag: udev device flag, e.g. %FU_IO_CHANNEL_OPEN_FLAG_READ
 *
 * Sets the parameters to use when opening the device.
 *
 * For example %FU_IO_CHANNEL_OPEN_FLAG_READ means that fu_device_open()
 * would use `O_RDONLY` rather than `O_RDWR` which is the default.
 *
 * Since: 2.0.0
 **/
void
fu_udev_device_add_open_flag(FuUdevDevice *self, FuIOChannelOpenFlags flag)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UDEV_DEVICE(self));

	/* already set */
	if (priv->open_flags & flag)
		return;
	priv->open_flags |= flag;
}

static gboolean
fu_udev_device_open(FuDevice *device, GError **error)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(device);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED))
		return TRUE;

	/* old versions of fwupd used to start with OPEN_READ|OPEN_WRITE and then plugins
	 * could add more flags, or set the flags back to NONE -- detect and fixup */
	if (priv->device_file != NULL && priv->open_flags == FU_IO_CHANNEL_OPEN_FLAG_NONE) {
#ifndef SUPPORTED_BUILD
		g_critical(
		    "%s [%s] forgot to call fu_udev_device_add_open_flag() with OPEN_READ and/or "
		    "OPEN_WRITE",
		    fu_device_get_name(device),
		    fu_device_get_id(device));
#endif
		fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
		fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	}

	/* open device */
	if (priv->device_file != NULL) {
		g_autoptr(FuIOChannel) io_channel = NULL;
		io_channel = fu_io_channel_new_file(priv->device_file, priv->open_flags, error);
		if (io_channel == NULL)
			return FALSE;
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
 * @timeout: timeout in ms for the retry action, see %FU_UDEV_DEVICE_IOCTL_FLAG_RETRY
 * @flags: some #FuUdevDeviceIoctlFlags, e.g. %FU_UDEV_DEVICE_IOCTL_FLAG_RETRY
 * @error: (nullable): optional return location for an error
 *
 * Control a device using a low-level request.
 *
 * NOTE: In version 2.0.0 the @bufsz parameter was added -- which isn't required to perform the
 * ioctl, but *is* required to accurately track and emulate the device buffer.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_udev_device_ioctl(FuUdevDevice *self,
		     gulong request,
		     guint8 *buf,
		     gsize bufsz,
		     gint *rc,
		     guint timeout,
		     FuUdevDeviceIoctlFlags flags,
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
	} while ((flags & FU_UDEV_DEVICE_IOCTL_FLAG_RETRY) && (errno == EINTR || errno == EAGAIN) &&
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
 * fu_udev_device_read_sysfs:
 * @self: a #FuUdevDevice
 * @attr: sysfs attribute name
 * @timeout_ms: IO timeout in milliseconds
 * @error: (nullable): optional return location for an error
 *
 * Reads data from a sysfs attribute, removing any newline trailing chars.
 *
 * Returns: (transfer full): string value, or %NULL
 *
 * Since: 2.0.0
 **/
gchar *
fu_udev_device_read_sysfs(FuUdevDevice *self, const gchar *attr, guint timeout_ms, GError **error)
{
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *value = NULL;
	g_autoptr(FuIOChannel) io_channel = NULL;
	g_autoptr(GByteArray) buf = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	g_return_val_if_fail(attr != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("ReadAttr:Attr=%s", attr);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		return g_strdup(fu_device_event_get_str(event, "Data", error));
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	/* open the file */
	if (fu_udev_device_get_sysfs_path(self) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "sysfs_path undefined");
		return NULL;
	}
	path = g_build_filename(fu_udev_device_get_sysfs_path(self), attr, NULL);
	io_channel = fu_io_channel_new_file(path, FU_IO_CHANNEL_OPEN_FLAG_READ, error);
	if (io_channel == NULL)
		return NULL;
	buf = fu_io_channel_read_byte_array(io_channel,
					    -1,
					    timeout_ms,
					    FU_IO_CHANNEL_FLAG_NONE,
					    error);
	if (buf == NULL)
		return NULL;
	if (!g_utf8_validate((const gchar *)buf->data, buf->len, NULL)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "non UTF-8 data");
		return NULL;
	}

	/* save response */
	value = g_strndup((const gchar *)buf->data, buf->len);

	/* remove the trailing newline */
	if (buf->len > 0) {
		if (value[buf->len - 1] == '\n')
			value[buf->len - 1] = '\0';
	}

	/* save for emulation */
	if (event != NULL)
		fu_device_event_set_str(event, "Data", value);

	/* success */
	return g_steal_pointer(&value);
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
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;
	g_autoptr(FuUdevDevice) new = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("GetParent:Subsystem=%s,Devtype=%s", subsystem, devtype);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		const gchar *sysfs_path_tmp;
		const gchar *devtype_tmp;
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		sysfs_path_tmp = fu_device_event_get_str(event, "SysfsPath", error);
		if (sysfs_path_tmp == NULL)
			return NULL;

		/* create a new device with this one acting as a proxy */
		new = fu_udev_device_new_from_sysfs_path(fu_device_get_context(FU_DEVICE(self)),
							 sysfs_path_tmp);
		fu_device_set_proxy(FU_DEVICE(new), FU_DEVICE(self));

		/* this is set as an optimization below, so copy behavior */
		devtype_tmp = fu_device_event_get_str(event, "Devtype", NULL);
		if (devtype_tmp != NULL)
			fu_udev_device_set_devtype(new, devtype_tmp);
		return g_steal_pointer(&new);
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	/* lets just walk up the directories */
	new = fu_udev_device_get_parent_with_subsystem_internal(self, subsystem, devtype, error);
	if (new == NULL)
		return NULL;

#ifndef SUPPORTED_BUILD
	if ((subsystem == NULL && fu_udev_device_get_subsystem(new) != NULL) &&
	    (devtype == NULL && fu_udev_device_get_devtype(new) != NULL)) {
		g_critical("fu_udev_device_get_parent_with_subsystem() called with ambiguity; "
			   "should have been %s, %s",
			   fu_udev_device_get_subsystem(new),
			   fu_udev_device_get_devtype(new));
	}
#endif

	/* save response */
	if (event != NULL) {
		fu_device_event_set_str(event, "SysfsPath", fu_udev_device_get_sysfs_path(new));
		fu_device_event_set_str(event, "Devtype", fu_udev_device_get_devtype(new));
	}

	/* success */
	return g_steal_pointer(&new);
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
#ifdef HAVE_GUSB
	guint64 bus = 0;
	guint64 address = 0;
	g_autofree gchar *attr_bus = NULL;
	g_autofree gchar *attr_dev = NULL;
	g_autoptr(FuUdevDevice) device_usb = NULL;
	g_autoptr(GUsbContext) usb_ctx = NULL;
	g_autoptr(GUsbDevice) usb_device = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* look at the current device and all the parent devices until we can find the USB data */
	device_usb = fu_udev_device_get_parent_with_subsystem(self, "usb", "usb_device", NULL);
	if (device_usb == NULL)
		return NULL;

	attr_bus = fu_udev_device_read_sysfs(device_usb,
					     "busnum",
					     FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					     error);
	if (attr_bus == NULL)
		return NULL;
	if (!fu_strtoull(attr_bus, &bus, 0, G_MAXUINT8, FU_INTEGER_BASE_16, error))
		return NULL;
	attr_dev = fu_udev_device_read_sysfs(device_usb,
					     "devnum",
					     FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					     error);
	if (attr_dev == NULL)
		return NULL;
	if (!fu_strtoull(attr_dev, &bus, 0, G_MAXUINT8, FU_INTEGER_BASE_16, error))
		return NULL;

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
			    "Not supported as <gusb.h> is unavailable");
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

/**
 * fu_udev_device_read_property:
 * @self: a #FuUdevDevice
 * @key: uevent key name, e.g. `HID_PHYS`
 * @error: (nullable): optional return location for an error
 *
 * Gets a value from the `uevent` file.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gchar *
fu_udev_device_read_property(FuUdevDevice *self, const gchar *key, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	FuDeviceEvent *event = NULL;
	g_autofree gchar *event_id = NULL;
	g_autofree gchar *value = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);
	g_return_val_if_fail(key != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("ReadProp:Key=%s", key);
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		return g_strdup(fu_device_event_get_str(event, "Data", error));
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	/* parse key */
	if (priv->uevent_lines == NULL) {
		g_autofree gchar *str = NULL;
		str = fu_udev_device_read_sysfs(self,
						"uevent",
						FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						error);
		if (str == NULL)
			return NULL;
		priv->uevent_lines = g_strsplit(str, "\n", -1);
	}
	for (guint i = 0; priv->uevent_lines[i] != NULL; i++) {
		g_auto(GStrv) kv = g_strsplit(priv->uevent_lines[i], "=", 2);
		if (g_strcmp0(kv[0], key) == 0) {
			value = g_strdup(kv[1]);
			break;
		}
	}
	if (value == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "uevent %s was not found",
			    key);
		return NULL;
	}

	/* save response */
	if (event != NULL)
		fu_device_event_set_str(event, "Data", value);

	/* success */
	return g_steal_pointer(&value);
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

	if (priv->uevent_lines != NULL)
		g_strfreev(priv->uevent_lines);
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

/**
 * fu_udev_device_new_from_sysfs_path:
 * @ctx: (nullable): a #FuContext
 * @sysfs_path: a sysfs path
 *
 * Creates a new #FuUdevDevice.
 *
 * Returns: (transfer full): a #FuUdevDevice
 *
 * Since: 2.0.0
 **/
FuUdevDevice *
fu_udev_device_new_from_sysfs_path(FuContext *ctx, const gchar *sysfs_path)
{
	return g_object_new(FU_TYPE_UDEV_DEVICE, "context", ctx, "backend-id", sysfs_path, NULL);
}
