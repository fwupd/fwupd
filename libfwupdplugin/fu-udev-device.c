/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuUdevDevice"

#include "config.h"

#include <string.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
	FuUdevDeviceFlags flags;
} FuUdevDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuUdevDevice, fu_udev_device, FU_TYPE_LINUX_DEVICE)

enum {
	PROP_0,
	PROP_UDEV_DEVICE,
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
	if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT32, &error_local)) {
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
	if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT16, &error_local)) {
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
	if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT8, &error_local)) {
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
	fwupd_codec_string_append_hex(str, idt, "UdevDeviceFlags", priv->flags);
}

static gboolean
fu_udev_device_ensure_bind_id(FuUdevDevice *self, GError **error)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *subsystem = fu_linux_device_get_subsystem(FU_LINUX_DEVICE(self));

	/* sanity check */
	if (fu_linux_device_get_bind_id(FU_LINUX_DEVICE(self)) != NULL)
		return TRUE;

#ifdef HAVE_GUDEV
	/* automatically set the bind ID from the subsystem */
	if (g_strcmp0(subsystem, "pci") == 0) {
		fu_linux_device_set_bind_id(
		    FU_LINUX_DEVICE(self),
		    g_udev_device_get_property(priv->udev_device, "PCI_SLOT_NAME"));
		return TRUE;
	}
	if (g_strcmp0(subsystem, "hid") == 0) {
		fu_linux_device_set_bind_id(
		    FU_LINUX_DEVICE(self),
		    g_udev_device_get_property(priv->udev_device, "HID_PHYS"));
		return TRUE;
	}
	if (g_strcmp0(subsystem, "usb") == 0) {
		g_autofree gchar *bind_id =
		    g_path_get_basename(g_udev_device_get_sysfs_path(priv->udev_device));
		fu_linux_device_set_bind_id(FU_LINUX_DEVICE(self), bind_id);
		return TRUE;
	}
#endif

	/* nothing found automatically */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "cannot derive bind-id from subsystem %s",
		    subsystem);
	return FALSE;
}

#ifdef HAVE_GUDEV
static const gchar *
fu_linux_device_get_vendor_fallback(GUdevDevice *udev_device)
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
	if (!fu_strtoull(str, &value, 0x0, G_MAXUINT16, NULL))
		return 0x0;
	return (guint16)value;
}

static void
fu_udev_device_set_vendor_from_udev_device(FuUdevDevice *self, GUdevDevice *udev_device)
{
	fu_linux_device_set_vendor(FU_LINUX_DEVICE(self),
				   fu_udev_device_get_sysfs_attr_as_uint16(udev_device, "vendor"));
	fu_linux_device_set_model(FU_LINUX_DEVICE(self),
				  fu_udev_device_get_sysfs_attr_as_uint16(udev_device, "device"));
	fu_linux_device_set_revision(
	    FU_LINUX_DEVICE(self),
	    fu_udev_device_get_sysfs_attr_as_uint8(udev_device, "revision"));
	fu_linux_device_set_pci_class(
	    FU_LINUX_DEVICE(self),
	    fu_udev_device_get_sysfs_attr_as_uint32(udev_device, "class"));
	fu_linux_device_set_subsystem_vendor(
	    FU_LINUX_DEVICE(self),
	    fu_udev_device_get_sysfs_attr_as_uint16(udev_device, "subsystem_vendor"));
	fu_linux_device_set_subsystem_model(
	    FU_LINUX_DEVICE(self),
	    fu_udev_device_get_sysfs_attr_as_uint16(udev_device, "subsystem_device"));

	/* fallback to properties as udev might be using a subsystem-specific prober */
	if (fu_linux_device_get_vendor(FU_LINUX_DEVICE(self)) == 0x0) {
		fu_linux_device_set_vendor(
		    FU_LINUX_DEVICE(self),
		    fu_udev_device_get_property_as_uint16(udev_device, "ID_VENDOR_ID"));
	}
	if (fu_linux_device_get_model(FU_LINUX_DEVICE(self)) == 0x0) {
		fu_linux_device_set_model(
		    FU_LINUX_DEVICE(self),
		    fu_udev_device_get_property_as_uint16(udev_device, "ID_MODEL_ID"));
	}
	if (fu_linux_device_get_revision(FU_LINUX_DEVICE(self)) == 0x0) {
		fu_linux_device_set_revision(
		    FU_LINUX_DEVICE(self),
		    fu_udev_device_get_property_as_uint16(udev_device, "ID_REVISION"));
	}
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
		if (fu_linux_device_get_vendor(FU_LINUX_DEVICE(self)) != 0x0 ||
		    fu_linux_device_get_model(FU_LINUX_DEVICE(self)) != 0x0 ||
		    fu_linux_device_get_revision(FU_LINUX_DEVICE(self)) != 0x0)
			break;
		g_set_object(&udev_device, parent);
	}
}
#endif

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
	if (udev_parent != NULL && fu_linux_device_get_vendor(FU_LINUX_DEVICE(self)) == 0x0 &&
	    fu_linux_device_get_model(FU_LINUX_DEVICE(self)) == 0x0 &&
	    fu_linux_device_get_revision(FU_LINUX_DEVICE(self)) == 0x0 &&
	    g_strcmp0(fu_linux_device_get_subsystem(FU_LINUX_DEVICE(self)), "hidraw") == 0) {
		tmp = g_udev_device_get_property(udev_parent, "HID_ID");
		if (tmp != NULL) {
			g_auto(GStrv) split = g_strsplit(tmp, ":", -1);
			if (g_strv_length(split) == 3) {
				guint64 val = g_ascii_strtoull(split[1], NULL, 16);
				if (val > G_MAXUINT16) {
					g_warning("reading %s for %s overflowed",
						  split[1],
						  g_udev_device_get_sysfs_path(priv->udev_device));
				} else {
					fu_linux_device_set_vendor(FU_LINUX_DEVICE(self), val);
				}
				val = g_ascii_strtoull(split[2], NULL, 16);
				if (val > G_MAXUINT32) {
					g_warning("reading %s for %s overflowed",
						  split[2],
						  g_udev_device_get_sysfs_path(priv->udev_device));
				} else {
					fu_linux_device_set_model(FU_LINUX_DEVICE(self), val);
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
	if (g_strcmp0(fu_linux_device_get_subsystem(FU_LINUX_DEVICE(self)), "pci") == 0 &&
	    fu_linux_device_is_pci_base_cls(FU_LINUX_DEVICE(device),
					    FU_LINUX_DEVICE_PCI_BASE_CLASS_DISPLAY) &&
	    fu_device_get_version(device) == NULL) {
		const gchar *version;
		version = g_udev_device_get_sysfs_attr(priv->udev_device, "vbios_version");
		if (version != NULL) {
			fu_device_set_version(device, version);
			fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_add_icon(FU_DEVICE(self), "video-display");
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

	fu_linux_device_set_devtype(FU_LINUX_DEVICE(self),
				    g_udev_device_get_devtype(priv->udev_device));
	//	fu_linux_device_set_revision(FU_LINUX_DEVICE(self),
	//				    g_udev_device_get_revision(priv->udev_device));

	/* set vendor */
	if (fu_device_get_vendor(device) == NULL) {
		tmp = fu_linux_device_get_vendor_fallback(priv->udev_device);
		if (tmp != NULL)
			fu_device_set_vendor(device, tmp);
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

	/* number */
	if (g_udev_device_get_number(priv->udev_device) != NULL) {
		guint64 tmp64 = 0;
		g_autoptr(GError) error_local = NULL;
		if (!fu_strtoull(g_udev_device_get_number(priv->udev_device),
				 &tmp64,
				 0x0,
				 G_MAXUINT64,
				 &error_local)) {
			g_warning("failed to convert udev number: %s", error_local->message);
		}
		fu_linux_device_set_number(FU_LINUX_DEVICE(self), tmp64);
	}

	fu_udev_device_ensure_bind_id(self, NULL);

	/* add firmware_id */
	if (g_strcmp0(g_udev_device_get_subsystem(priv->udev_device), "serio") == 0) {
		if (!fu_udev_device_probe_serio(self, error))
			return FALSE;
	}

	/* determine if we're wired internally */
	parent_i2c = g_udev_device_get_parent_with_subsystem(priv->udev_device, "i2c", NULL);
	if (parent_i2c != NULL)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);

	/* try harder to find a vendor name the user will recognize */
	if (priv->flags & FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT && udev_parent != NULL &&
	    fu_device_get_vendor(device) == NULL) {
		g_autoptr(GUdevDevice) device_tmp = g_object_ref(udev_parent);
		for (guint i = 0; i < 0xff; i++) {
			g_autoptr(GUdevDevice) parent = NULL;
			tmp = fu_linux_device_get_vendor_fallback(device_tmp);
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
#endif
	/* FuLinuxDevice */
	if (!FU_DEVICE_CLASS(fu_udev_device_parent_class)->probe(device, error))
		return FALSE;

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
	fu_linux_device_set_sysfs_path(FU_LINUX_DEVICE(self),
				       g_udev_device_get_sysfs_path(priv->udev_device));
	fu_linux_device_set_subsystem(FU_LINUX_DEVICE(self),
				      g_udev_device_get_subsystem(priv->udev_device));
	fu_linux_device_set_driver(FU_LINUX_DEVICE(self),
				   g_udev_device_get_driver(priv->udev_device));
	fu_linux_device_set_device_file(FU_LINUX_DEVICE(self),
					g_udev_device_get_device_file(priv->udev_device));

	/* so we can display something sensible for unclaimed devices */
	fu_device_set_backend_id(FU_DEVICE(self), g_udev_device_get_sysfs_path(priv->udev_device));

	/* fall back to the first thing handled by misc drivers */
	if (fu_linux_device_get_device_file(FU_LINUX_DEVICE(self)) == NULL) {
		/* perhaps we should unconditionally fall back? or perhaps
		 * require FU_UDEV_DEVICE_FLAG_FALLBACK_MISC... */
		if (g_strcmp0(fu_linux_device_get_subsystem(FU_LINUX_DEVICE(self)), "serio") == 0) {
			g_autofree gchar *device_file = fu_udev_device_get_miscdev0(self);
			fu_linux_device_set_device_file(FU_LINUX_DEVICE(self), device_file);
		}
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
 * fu_udev_device_get_slot_depth:
 * @self: a #FuUdevDevice
 * @subsystem: a subsystem
 *
 * Determine how far up a chain a given device is
 *
 * Returns: unsigned integer
 *
 * Since: 1.2.4
 **/
guint
fu_udev_device_get_slot_depth(FuUdevDevice *self, const gchar *subsystem)
{
#ifdef HAVE_GUDEV
	GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(self));
	g_autoptr(GUdevDevice) device_tmp = NULL;

	device_tmp = g_udev_device_get_parent_with_subsystem(udev_device, subsystem, NULL);
	if (device_tmp == NULL)
		return 0;
	for (guint i = 0; i < 0xff; i++) {
		g_autoptr(GUdevDevice) parent = g_udev_device_get_parent(device_tmp);
		if (parent == NULL)
			return i;
		g_set_object(&device_tmp, parent);
	}
#endif
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

static void
fu_udev_device_incorporate(FuDevice *self, FuDevice *donor)
{
	FuUdevDevice *uself = FU_UDEV_DEVICE(self);
	FuUdevDevice *udonor = FU_UDEV_DEVICE(donor);

	g_return_if_fail(FU_IS_UDEV_DEVICE(self));
	g_return_if_fail(FU_IS_UDEV_DEVICE(donor));

	fu_udev_device_set_dev(uself, fu_udev_device_get_dev(udonor));
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

#ifdef HAVE_GUDEV
static gchar *
fu_udev_device_get_parent_subsystems(FuUdevDevice *self)
{
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	GString *str = g_string_new(NULL);
	g_autoptr(GUdevDevice) udev_device = g_object_ref(priv->udev_device);

	/* find subsystems of self and all parent devices */
	if (fu_linux_device_get_subsystem(FU_LINUX_DEVICE(self)) != NULL) {
		g_string_append_printf(str,
				       "%s,",
				       fu_linux_device_get_subsystem(FU_LINUX_DEVICE(self)));
	}
	while (TRUE) {
		g_autoptr(GUdevDevice) parent = g_udev_device_get_parent(udev_device);
		if (parent == NULL)
			break;
		if (g_udev_device_get_subsystem(parent) != NULL) {
			g_string_append_printf(str, "%s,", g_udev_device_get_subsystem(parent));
		}
		g_set_object(&udev_device, g_steal_pointer(&parent));
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

static GUdevDevice *
fu_udev_device_get_parent_with_subsystem_devtype(GUdevDevice *udev_device,
						 const gchar *subsystem,
						 const gchar *devtype)
{
	g_autoptr(GUdevDevice) udev_device_tmp = g_object_ref(udev_device);
	while (udev_device_tmp != NULL) {
		g_autoptr(GUdevDevice) parent = NULL;
		if (fu_udev_device_match_subsystem_devtype(udev_device_tmp, subsystem, devtype))
			return g_object_ref(udev_device_tmp);
		parent = g_udev_device_get_parent(udev_device_tmp);
		g_set_object(&udev_device_tmp, parent);
	}
	return NULL;
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

		/* matching on devtype is optional */
		udev_device = fu_udev_device_get_parent_with_subsystem_devtype(priv->udev_device,
									       subsys_devtype[0],
									       subsys_devtype[1]);
		if (udev_device != NULL) {
			subsystem = g_strdup(subsys_devtype[0]);
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
	if (g_strcmp0(fu_linux_device_get_subsystem(FU_LINUX_DEVICE(self)), subsystem) == 0) {
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
 * fu_udev_device_remove_flag:
 * @self: a #FuUdevDevice
 * @flag: udev device flag, e.g. %FU_LINUX_DEVICE_FLAG_OPEN_READ
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
 * @flag: udev device flag, e.g. %FU_LINUX_DEVICE_FLAG_OPEN_READ
 *
 * Sets the parameters to use when opening the device.
 *
 * For example %FU_LINUX_DEVICE_FLAG_OPEN_READ means that fu_device_open()
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

#ifdef HAVE_GUDEV
	/* overwrite */
	if (flag & FU_UDEV_DEVICE_FLAG_USE_CONFIG) {
		g_autofree gchar *device_file =
		    g_build_filename(g_udev_device_get_sysfs_path(priv->udev_device),
				     "config",
				     NULL);
		fu_linux_device_set_device_file(FU_LINUX_DEVICE(self), device_file);
	}
#endif
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

/**
 * fu_udev_device_get_parent_name
 * @self: a #FuUdevDevice
 *
 * Returns the name of the direct ancestor of this device
 *
 * Returns: string or NULL if unset or invalid
 *
 * Since: 1.4.5
 **/
gchar *
fu_udev_device_get_parent_name(FuUdevDevice *self)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GUdevDevice) parent = NULL;

	g_return_val_if_fail(FU_IS_UDEV_DEVICE(self), NULL);

	if (priv->udev_device == NULL)
		return NULL;
	parent = g_udev_device_get_parent(priv->udev_device);
	return parent == NULL ? NULL : g_strdup(g_udev_device_get_name(parent));
#else
	return NULL;
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

	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), FALSE);
	g_return_val_if_fail(attr != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	tmp = fu_udev_device_get_sysfs_attr(self, attr, error);
	if (tmp == NULL)
		return FALSE;
	return fu_strtoull(tmp, value, 0, G_MAXUINT64, error);
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
 * @subsystem: (nullable): the name of a udev subsystem
 * @error: (nullable): optional return location for an error
 *
 * Get the device that is a parent of self and has the provided subsystem.
 *
 * Returns: (transfer full): device, or %NULL
 *
 * Since: 2.0.0
 */
FuUdevDevice *
fu_udev_device_get_parent_with_subsystem(FuUdevDevice *self, const gchar *subsystem, GError **error)
{
#ifdef HAVE_GUDEV
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GUdevDevice) device_tmp = NULL;

	/* sanity check */
	if (priv->udev_device == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "not initialized");
		return NULL;
	}
	if (subsystem == NULL) {
		device_tmp = g_udev_device_get_parent(priv->udev_device);
	} else {
		device_tmp =
		    g_udev_device_get_parent_with_subsystem(priv->udev_device, subsystem, NULL);
	}
	if (device_tmp == NULL) {
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
	const gchar *self_path = fu_linux_device_get_sysfs_path(FU_LINUX_DEVICE(self));
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

static void
fu_udev_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuUdevDevice *self = FU_UDEV_DEVICE(object);
	FuUdevDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_UDEV_DEVICE:
		g_value_set_object(value, priv->udev_device);
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

	if (priv->udev_device != NULL)
		g_object_unref(priv->udev_device);

	G_OBJECT_CLASS(fu_udev_device_parent_class)->finalize(object);
}

static void
fu_udev_device_init(FuUdevDevice *self)
{
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
	device_class->to_string = fu_udev_device_to_string;
	device_class->probe_complete = fu_udev_device_probe_complete;

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
