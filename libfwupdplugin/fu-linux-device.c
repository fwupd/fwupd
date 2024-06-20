/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuLinuxDevice"

#include "config.h"

#include <fcntl.h>
#include <glib/gstdio.h>
#ifdef HAVE_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include "fu-device-event-private.h"
#include "fu-device-private.h"
#include "fu-linux-device.h"

typedef struct {
	//	gchar *sysfs_path;
	gchar *subsystem;
	gchar *bind_id;
	gchar *driver;
	gchar *device_file;
	gchar *devtype;
	guint32 pci_class;
	guint16 vendor;
	guint16 model;
	guint16 subsystem_vendor;
	guint16 subsystem_model;
	guint8 revision;
	guint64 number;
	FuIOChannel *io_channel;
	FuLinuxDeviceFlags flags;
} FuLinuxDevicePrivate;

static void
fu_linux_device_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuLinuxDevice,
		       fu_linux_device,
		       FU_TYPE_DEVICE,
		       0,
		       G_ADD_PRIVATE(FuLinuxDevice)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC,
						 fu_linux_device_codec_iface_init));

enum {
	PROP_0,
	PROP_SYSFS_PATH,
	PROP_SUBSYSTEM,
	PROP_DRIVER,
	PROP_DEVICE_FILE,
	PROP_DEVTYPE,
	PROP_BIND_ID,
	PROP_LAST
};

#define GET_PRIVATE(o) (fu_linux_device_get_instance_private(o))

/**
 * fu_linux_device_get_sysfs_path:
 * @self: a #FuLinuxDevice
 *
 * Gets the device sysfs path, e.g. `/sys/devices/pci0000:00/0000:00:14.0`.
 *
 * Returns: a local path, or NULL if unset
 *
 * Since: 2.0.0
 **/
const gchar *
fu_linux_device_get_sysfs_path(FuLinuxDevice *self)
{
	return fu_device_get_backend_id(FU_DEVICE(self));
#if 0
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), NULL);
	return priv->sysfs_path;
#endif
}

/**
 * fu_linux_device_set_sysfs_path:
 * @self: a #FuLinuxDevice
 * @sysfs_path: a local path
 *
 * Sets the device sysfs path, e.g. `/sys/devices/pci0000:00/0000:00:14.0`.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_set_sysfs_path(FuLinuxDevice *self, const gchar *sysfs_path)
{
	fu_device_set_backend_id(FU_DEVICE(self), sysfs_path);
#if 0
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_LINUX_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->sysfs_path, sysfs_path) == 0)
		return;
	g_free(priv->sysfs_path);
	priv->sysfs_path = g_strdup(sysfs_path);
	g_object_notify(G_OBJECT(self), "sysfs-path");
#endif
}

/**
 * fu_linux_device_get_device_file:
 * @self: a #FuLinuxDevice
 *
 * Gets the device node.
 *
 * Returns: a device file, or NULL if unset
 *
 * Since: 2.0.0
 **/
const gchar *
fu_linux_device_get_device_file(FuLinuxDevice *self)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), NULL);
	return priv->device_file;
}

/**
 * fu_linux_device_set_device_file:
 * @self: a #FuLinuxDevice
 * @device_file: (nullable): a device path
 *
 * Sets the device file to use for reading and writing.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_set_device_file(FuLinuxDevice *self, const gchar *device_file)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->device_file, device_file) == 0)
		return;

	g_free(priv->device_file);
	priv->device_file = g_strdup(device_file);
	g_object_notify(G_OBJECT(self), "device-file");
}

/**
 * fu_linux_device_get_devtype:
 * @self: a #FuLinuxDevice
 *
 * Gets the device type specified in the uevent.
 *
 * Returns: a device file, or NULL if unset
 *
 * Since: 2.0.0
 **/
const gchar *
fu_linux_device_get_devtype(FuLinuxDevice *self)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), NULL);
	return priv->devtype;
}

/**
 * fu_linux_device_set_devtype:
 * @self: a #FuLinuxDevice
 * @devtype: (nullable): a device path
 *
 * Sets the device type specified in the uevent.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_set_devtype(FuLinuxDevice *self, const gchar *devtype)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->devtype, devtype) == 0)
		return;

	g_free(priv->devtype);
	priv->devtype = g_strdup(devtype);
	g_object_notify(G_OBJECT(self), "device-file");
}

/**
 * fu_linux_device_get_subsystem:
 * @self: a #FuLinuxDevice
 *
 * Gets the device subsystem, e.g. `pci`
 *
 * Returns: a subsystem, or NULL if unset or invalid
 *
 * Since: 2.0.0
 **/
const gchar *
fu_linux_device_get_subsystem(FuLinuxDevice *self)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), NULL);
	return priv->subsystem;
}

/**
 * fu_linux_device_set_subsystem:
 * @self: a #FuLinuxDevice
 * @subsystem: a subsystem string, e.g. `hidraw`
 *
 * Sets the device subsystem.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_set_subsystem(FuLinuxDevice *self, const gchar *subsystem)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->subsystem, subsystem) == 0)
		return;

	g_free(priv->subsystem);
	priv->subsystem = g_strdup(subsystem);
	g_object_notify(G_OBJECT(self), "subsystem");
}

/**
 * fu_linux_device_get_bind_id:
 * @self: a #FuLinuxDevice
 *
 * Gets the device ID used for binding the device, e.g. `pci:1:2:3`
 *
 * Returns: a bind_id, or NULL if unset or invalid
 *
 * Since: 2.0.0
 **/
const gchar *
fu_linux_device_get_bind_id(FuLinuxDevice *self)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), NULL);
	return priv->bind_id;
}

/**
 * fu_linux_device_set_bind_id:
 * @self: a #FuLinuxDevice
 * @bind_id: a bind-id string, e.g. `pci:0:0:1`
 *
 * Sets the device ID used for binding the device.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_set_bind_id(FuLinuxDevice *self, const gchar *bind_id)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->bind_id, bind_id) == 0)
		return;

	g_free(priv->bind_id);
	priv->bind_id = g_strdup(bind_id);
	g_object_notify(G_OBJECT(self), "bind-id");
}

/**
 * fu_linux_device_get_driver:
 * @self: a #FuLinuxDevice
 *
 * Gets the device driver, e.g. `psmouse`.
 *
 * Returns: a subsystem, or NULL if unset or invalid
 *
 * Since: 2.0.0
 **/
const gchar *
fu_linux_device_get_driver(FuLinuxDevice *self)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), NULL);
	return priv->driver;
}

/**
 * fu_linux_device_set_driver:
 * @self: a #FuLinuxDevice
 * @driver: a driver name, e.g. `eth1000`
 *
 * Sets the driver used for managing the device.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_set_driver(FuLinuxDevice *self, const gchar *driver)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->driver, driver) == 0)
		return;

	g_free(priv->driver);
	priv->driver = g_strdup(driver);
	g_object_notify(G_OBJECT(self), "driver");
}

/**
 * fu_linux_device_get_pci_class:
 * @self: a #FuLinuxDevice
 *
 * Gets the PCI class for a device.
 *
 * The class consists of a base class and subclass.
 *
 * Returns: a PCI class
 *
 * Since: 2.0.0
 **/
guint32
fu_linux_device_get_pci_class(FuLinuxDevice *self)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), 0x0000);
	return priv->pci_class;
}

/**
 * fu_linux_device_set_pci_class:
 * @self: a #FuLinuxDevice
 * @pci_class: integer
 *
 * Sets the PCI class.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_set_pci_class(FuLinuxDevice *self, guint32 pci_class)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_LINUX_DEVICE(self));
	priv->pci_class = pci_class;
}

/**
 * fu_linux_device_get_vendor:
 * @self: a #FuLinuxDevice
 *
 * Gets the device vendor code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 2.0.0
 **/
guint16
fu_linux_device_get_vendor(FuLinuxDevice *self)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), 0x0000);
	return priv->vendor;
}

/**
 * fu_linux_device_set_vendor:
 * @self: a #FuLinuxDevice
 * @vendor: integer
 *
 * Sets the device vendor code.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_set_vendor(FuLinuxDevice *self, guint16 vendor)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_LINUX_DEVICE(self));
	priv->vendor = vendor;
}

/**
 * fu_linux_device_get_model:
 * @self: a #FuLinuxDevice
 *
 * Gets the device model code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 2.0.0
 **/
guint16
fu_linux_device_get_model(FuLinuxDevice *self)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), 0x0000);
	return priv->model;
}

/**
 * fu_linux_device_set_model:
 * @self: a #FuLinuxDevice
 * @model: integer
 *
 * Sets the device model code.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_set_model(FuLinuxDevice *self, guint16 model)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_LINUX_DEVICE(self));
	priv->model = model;
}

/**
 * fu_linux_device_get_subsystem_vendor:
 * @self: a #FuLinuxDevice
 *
 * Gets the device subsystem vendor code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 2.0.0
 **/
guint16
fu_linux_device_get_subsystem_vendor(FuLinuxDevice *self)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), 0x0000);
	return priv->subsystem_vendor;
}

/**
 * fu_linux_device_set_subsystem_vendor:
 * @self: a #FuLinuxDevice
 * @subsystem_vendor: integer
 *
 * Sets the device subsystem vendor code.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_set_subsystem_vendor(FuLinuxDevice *self, guint16 subsystem_vendor)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_LINUX_DEVICE(self));
	priv->subsystem_vendor = subsystem_vendor;
}

/**
 * fu_linux_device_get_subsystem_model:
 * @self: a #FuLinuxDevice
 *
 * Gets the device subsystem model code.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 2.0.0
 **/
guint16
fu_linux_device_get_subsystem_model(FuLinuxDevice *self)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), 0x0000);
	return priv->subsystem_model;
}

/**
 * fu_linux_device_set_subsystem_model:
 * @self: a #FuLinuxDevice
 * @subsystem_model: integer
 *
 * Sets the device subsystem model code.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_set_subsystem_model(FuLinuxDevice *self, guint16 subsystem_model)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_LINUX_DEVICE(self));
	priv->subsystem_model = subsystem_model;
}

/**
 * fu_linux_device_get_revision:
 * @self: a #FuLinuxDevice
 *
 * Gets the device revision.
 *
 * Returns: a vendor code, or 0 if unset or invalid
 *
 * Since: 2.0.0
 **/
guint8
fu_linux_device_get_revision(FuLinuxDevice *self)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), 0x00);
	return priv->revision;
}

/**
 * fu_linux_device_set_revision:
 * @self: a #FuLinuxDevice
 * @revision: integer
 *
 * Sets the device revision.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_set_revision(FuLinuxDevice *self, guint8 revision)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_LINUX_DEVICE(self));
	priv->revision = revision;
}

/**
 * fu_linux_device_get_number:
 * @self: a #FuLinuxDevice
 *
 * Gets the device number, if any.
 *
 * Returns: integer, 0 if the data is unavailable.
 *
 * Since: 2.0.0
 **/
guint64
fu_linux_device_get_number(FuLinuxDevice *self)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), 0x0000);
	return priv->number;
}

/**
 * fu_linux_device_set_number:
 * @self: a #FuLinuxDevice
 * @number: integer
 *
 * Sets the number.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_set_number(FuLinuxDevice *self, guint64 number)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_LINUX_DEVICE(self));
	priv->number = number;
}

/**
 * fu_linux_device_ioctl:
 * @self: a #FuLinuxDevice
 * @request: request number
 * @buf: a buffer to use, which *must* be large enough for the request
 * @bufsz: the size of @buf
 * @rc: (out) (nullable): the raw return value from the ioctl
 * @timeout: timeout in ms for the retry action, see %FU_LINUX_DEVICE_FLAG_IOCTL_RETRY
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
fu_linux_device_ioctl(FuLinuxDevice *self,
		      gulong request,
		      guint8 *buf,
		      gsize bufsz,
		      gint *rc,
		      guint timeout,
		      GError **error)
{
#ifdef HAVE_IOCTL_H
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	gint rc_tmp;
	g_autoptr(GTimer) timer = g_timer_new();
	FuDeviceEvent *event = NULL;
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	g_autofree gchar *event_id = NULL;

	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), FALSE);
	g_return_val_if_fail(request != 0x0, FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS)) {
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
	if (fu_context_has_flag(ctx, FU_CONTEXT_FLAG_SAVE_EVENTS)) {
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
	} while ((priv->flags & FU_LINUX_DEVICE_FLAG_IOCTL_RETRY) &&
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
 * fu_linux_device_pread:
 * @self: a #FuLinuxDevice
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
fu_linux_device_pread(FuLinuxDevice *self, goffset port, guint8 *buf, gsize bufsz, GError **error)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

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
 * fu_linux_device_seek:
 * @self: a #FuLinuxDevice
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
fu_linux_device_seek(FuLinuxDevice *self, goffset offset, GError **error)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

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
 * fu_linux_device_pwrite:
 * @self: a #FuLinuxDevice
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
fu_linux_device_pwrite(FuLinuxDevice *self,
		       goffset port,
		       const guint8 *buf,
		       gsize bufsz,
		       GError **error)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

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

static gboolean
fu_linux_device_unbind_driver(FuDevice *device, GError **error)
{
#ifdef HAVE_GUDEV
	FuLinuxDevice *self = FU_LINUX_DEVICE(device);
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GOutputStream) stream = NULL;

	/* is already unbound */
	if (fu_linux_device_get_sysfs_path(self) == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "not initialized");
		return FALSE;
	}
	fn = g_build_filename(fu_linux_device_get_sysfs_path(self), "driver", "unbind", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS))
		return TRUE;

	/* write bus ID to file */
	//	if (!fu_linux_device_ensure_bind_id(self, error))
	//		return FALSE;
	// FIXME
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
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "driver unbinding not supported");
	return FALSE;
#endif
}

static gboolean
fu_linux_device_bind_driver(FuDevice *device,
			    const gchar *subsystem,
			    const gchar *driver,
			    GError **error)
{
#ifdef HAVE_GUDEV
	FuLinuxDevice *self = FU_LINUX_DEVICE(device);
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *driver_safe = g_strdup(driver);
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GOutputStream) stream = NULL;

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
	//	if (!fu_linux_device_ensure_bind_id(self, error))
	//		return FALSE;
	// FIXME
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
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "driver binding not supported on Windows");
	return FALSE;
#endif
}

/**
 * fu_linux_device_get_io_channel:
 * @self: a #FuLinuxDevice
 *
 * Gets the IO channel.
 *
 * Returns: (transfer none): a #FuIOChannel, or %NULL if the device is not open
 *
 * Since: 1.9.8
 **/
FuIOChannel *
fu_linux_device_get_io_channel(FuLinuxDevice *self)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), NULL);
	return priv->io_channel;
}

/**
 * fu_linux_device_set_io_channel:
 * @self: a #FuLinuxDevice
 * @io_channel: a #FuIOChannel
 *
 * Replace the IO channel to use when the device has already been opened.
 * This object will automatically unref @io_channel when fu_device_close() is called.
 *
 * Since: 1.9.8
 **/
void
fu_linux_device_set_io_channel(FuLinuxDevice *self, FuIOChannel *io_channel)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_LINUX_DEVICE(self));
	g_return_if_fail(FU_IS_IO_CHANNEL(io_channel));
	g_set_object(&priv->io_channel, io_channel);
}

/**
 * fu_linux_device_remove_flag:
 * @self: a #FuLinuxDevice
 * @flag: udev device flag, e.g. %FU_LINUX_DEVICE_FLAG_OPEN_READ
 *
 * Removes a parameters flag.
 *
 * Since: 2.0.0
 **/
void
fu_linux_device_remove_flag(FuLinuxDevice *self, FuLinuxDeviceFlags flag)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_LINUX_DEVICE(self));
	priv->flags &= ~flag;
}

/**
 * fu_linux_device_add_flag:
 * @self: a #FuLinuxDevice
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
fu_linux_device_add_flag(FuLinuxDevice *self, FuLinuxDeviceFlags flag)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_LINUX_DEVICE(self));

	/* already set */
	if (priv->flags & flag)
		return;
	priv->flags |= flag;
}

static gboolean
fu_linux_device_open(FuDevice *device, GError **error)
{
	FuLinuxDevice *self = FU_LINUX_DEVICE(device);
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	gint fd;

	/* old versions of fwupd used to start with OPEN_READ|OPEN_WRITE and then plugins
	 * could add more flags, or set the flags back to NONE -- detect and fixup */
	if (priv->device_file != NULL && priv->flags == FU_LINUX_DEVICE_FLAG_NONE) {
#ifndef SUPPORTED_BUILD
		g_critical(
		    "%s [%s] forgot to call fu_linux_device_add_flag() with OPEN_READ and/or "
		    "OPEN_WRITE",
		    fu_device_get_name(device),
		    fu_device_get_id(device));
#endif
		fu_linux_device_add_flag(FU_LINUX_DEVICE(self), FU_LINUX_DEVICE_FLAG_OPEN_READ);
		fu_linux_device_add_flag(FU_LINUX_DEVICE(self), FU_LINUX_DEVICE_FLAG_OPEN_WRITE);
	}

	/* open device */
	if (priv->device_file != NULL && priv->flags != FU_LINUX_DEVICE_FLAG_NONE) {
		gint flags;
		g_autoptr(FuIOChannel) io_channel = NULL;
		if (priv->flags & FU_LINUX_DEVICE_FLAG_OPEN_READ &&
		    priv->flags & FU_LINUX_DEVICE_FLAG_OPEN_WRITE) {
			flags = O_RDWR;
		} else if (priv->flags & FU_LINUX_DEVICE_FLAG_OPEN_WRITE) {
			flags = O_WRONLY;
		} else {
			flags = O_RDONLY;
		}
#ifdef O_NONBLOCK
		if (priv->flags & FU_LINUX_DEVICE_FLAG_OPEN_NONBLOCK)
			flags |= O_NONBLOCK;
#endif
#ifdef O_SYNC
		if (priv->flags & FU_LINUX_DEVICE_FLAG_OPEN_SYNC)
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
fu_linux_device_close(FuDevice *device, GError **error)
{
	FuLinuxDevice *self = FU_LINUX_DEVICE(device);
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->io_channel != NULL) {
		if (!fu_io_channel_shutdown(priv->io_channel, error))
			return FALSE;
	}
	return TRUE;
}

/**
 * fu_linux_device_write_attr:
 * @self: a #FuLinuxDevice
 * @attribute: sysfs attribute name
 * @val: data to write into the attribute
 * @error: (nullable): optional return location for an error
 *
 * Writes data into a sysfs attribute
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_linux_device_write_attr(FuLinuxDevice *self,
			   const gchar *attribute,
			   const gchar *val,
			   GError **error)
{
	//	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *path = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileOutputStream) output_stream = NULL;

	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), FALSE);
	g_return_val_if_fail(attribute != NULL, FALSE);
	g_return_val_if_fail(val != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* open the file */
	if (fu_linux_device_get_sysfs_path(self) == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Unable to write %s as sysfs_path undefined",
			    attribute);
		return FALSE;
	}
	path = g_build_filename(fu_linux_device_get_sysfs_path(self), attribute, NULL);
	file = g_file_new_for_path(path);
	output_stream = g_file_replace(file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error);
	if (output_stream == NULL) {
		fu_error_convert(error);
		return FALSE;
	}
	if (!g_output_stream_write_all(G_OUTPUT_STREAM(output_stream),
				       val,
				       strlen(val),
				       NULL,
				       NULL,
				       error)) {
		fu_error_convert(error);
		return FALSE;
	}
	/* success */
	return TRUE;
}

static GBytes *
fu_linux_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLinuxDevice *self = FU_LINUX_DEVICE(device);
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
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
 * fu_linux_device_is_pci_base_cls:
 * @self: a #FuLinuxDevice
 * @pci_base_class: #FuLinuxDevicePciBaseClass type
 *
 * Determines whether the device matches a given pci base class type
 *
 * Since: 2.0.0
 **/
gboolean
fu_linux_device_is_pci_base_cls(FuLinuxDevice *self, FuLinuxDevicePciBaseClass pci_base_class)
{
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_LINUX_DEVICE(self), FALSE);
	return (priv->pci_class >> 16) == pci_base_class;
}

static void
fu_linux_device_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FuDevice *device = FU_DEVICE(codec);
	FuLinuxDevice *self = FU_LINUX_DEVICE(codec);
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	GPtrArray *events = fu_device_get_events(device);

	/* optional properties */
	if (fu_linux_device_get_sysfs_path(self) != NULL)
		fwupd_codec_json_append(builder, "BackendId", fu_linux_device_get_sysfs_path(self));
	if (priv->subsystem != NULL)
		fwupd_codec_json_append(builder, "Subsystem", priv->subsystem);
	if (priv->driver != NULL)
		fwupd_codec_json_append(builder, "Driver", priv->driver);
	if (priv->bind_id != NULL)
		fwupd_codec_json_append(builder, "BindId", priv->bind_id);
	if (priv->device_file != NULL)
		fwupd_codec_json_append(builder, "DeviceFile", priv->device_file);
	if (priv->vendor != 0)
		fwupd_codec_json_append_int(builder, "Vendor", priv->vendor);
	if (priv->model != 0)
		fwupd_codec_json_append_int(builder, "Model", priv->model);
	if (priv->subsystem_vendor != 0)
		fwupd_codec_json_append_int(builder, "SubsystemVendor", priv->subsystem_vendor);
	if (priv->subsystem_model != 0)
		fwupd_codec_json_append_int(builder, "SubsystemModel", priv->subsystem_model);
	if (priv->pci_class != 0)
		fwupd_codec_json_append_int(builder, "PciClass", priv->pci_class);
	if (priv->revision != 0)
		fwupd_codec_json_append_int(builder, "Revision", priv->revision);

#if GLIB_CHECK_VERSION(2, 62, 0)
	/* created */
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
fu_linux_device_from_json(FwupdCodec *codec, JsonNode *json_node, GError **error)
{
	FuDevice *device = FU_DEVICE(codec);
	FuLinuxDevice *self = FU_LINUX_DEVICE(codec);
	JsonObject *json_object = json_node_get_object(json_node);
	const gchar *tmp;
	gint64 tmp64;

	tmp = json_object_get_string_member_with_default(json_object, "BackendId", NULL);
	if (tmp != NULL)
		fu_linux_device_set_sysfs_path(self, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "Subsystem", NULL);
	if (tmp != NULL)
		fu_linux_device_set_subsystem(self, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "Driver", NULL);
	if (tmp != NULL)
		fu_linux_device_set_driver(self, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "BindId", NULL);
	if (tmp != NULL)
		fu_linux_device_set_bind_id(self, tmp);
	tmp = json_object_get_string_member_with_default(json_object, "DeviceFile", NULL);
	if (tmp != NULL)
		fu_linux_device_set_device_file(self, tmp);
	tmp64 = json_object_get_int_member_with_default(json_object, "Vendor", 0);
	if (tmp64 != 0)
		fu_linux_device_set_vendor(self, tmp64);
	tmp64 = json_object_get_int_member_with_default(json_object, "Model", 0);
	if (tmp64 != 0)
		fu_linux_device_set_model(self, tmp64);
	tmp64 = json_object_get_int_member_with_default(json_object, "SubsystemVendor", 0);
	if (tmp64 != 0)
		fu_linux_device_set_subsystem_vendor(self, tmp64);
	tmp64 = json_object_get_int_member_with_default(json_object, "SubsystemModel", 0);
	if (tmp64 != 0)
		fu_linux_device_set_subsystem_model(self, tmp64);
	tmp64 = json_object_get_int_member_with_default(json_object, "PciClass", 0);
	if (tmp64 != 0)
		fu_linux_device_set_pci_class(self, tmp64);
	tmp64 = json_object_get_int_member_with_default(json_object, "Revision", 0);
	if (tmp64 != 0)
		fu_linux_device_set_revision(self, tmp64);

	/* created */
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
fu_linux_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuLinuxDevice *self = FU_LINUX_DEVICE(object);
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_SYSFS_PATH:
		g_value_set_string(value, fu_linux_device_get_sysfs_path(self));
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
fu_linux_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuLinuxDevice *self = FU_LINUX_DEVICE(object);
	switch (prop_id) {
	case PROP_SYSFS_PATH:
		fu_linux_device_set_sysfs_path(self, g_value_get_string(value));
		break;
	case PROP_SUBSYSTEM:
		fu_linux_device_set_subsystem(self, g_value_get_string(value));
		break;
	case PROP_BIND_ID:
		fu_linux_device_set_bind_id(self, g_value_get_string(value));
		break;
	case PROP_DRIVER:
		fu_linux_device_set_driver(self, g_value_get_string(value));
		break;
	case PROP_DEVICE_FILE:
		fu_linux_device_set_device_file(self, g_value_get_string(value));
		break;
	case PROP_DEVTYPE:
		fu_linux_device_set_devtype(self, g_value_get_string(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_linux_device_finalize(GObject *object)
{
	FuLinuxDevice *self = FU_LINUX_DEVICE(object);
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->io_channel != NULL)
		g_object_unref(priv->io_channel);
	//	g_free(priv->sysfs_path);
	g_free(priv->subsystem);
	g_free(priv->bind_id);
	g_free(priv->driver);
	g_free(priv->device_file);
	G_OBJECT_CLASS(fu_linux_device_parent_class)->finalize(object);
}

static gboolean
fu_linux_device_probe(FuDevice *device, GError **error)
{
	FuLinuxDevice *self = FU_LINUX_DEVICE(device);
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *subsystem = NULL;

	/* set the version if the revision has been set */
	if (fu_device_get_version(device) == NULL &&
	    fu_device_get_version_format(device) == FWUPD_VERSION_FORMAT_UNKNOWN) {
		if (priv->revision != 0x00 && priv->revision != 0xFF) {
			g_autofree gchar *version = g_strdup_printf("%02x", priv->revision);
			fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_set_version_raw(device, priv->revision);
			fu_device_set_version(device, version);
		}
	}

	/* set vendor ID */
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

	/* add device class */
	if (subsystem != NULL) {
#if 0
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
#endif

		/* add devtype */
		fu_device_add_instance_strup(device, "TYPE", priv->devtype);
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

		/* add the modalias */
		//		fu_device_add_instance_strsafe(
		//		    device,
		//		    "MODALIAS",
		//		    g_udev_device_get_property(priv->udev_device, "MODALIAS"));
		//		fu_device_build_instance_id_full(device,
		//						 FU_DEVICE_INSTANCE_FLAG_GENERIC |
		//						     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
		//						 NULL,
		//						 subsystem,
		//						 "MODALIAS",
		//						 NULL);
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

	/* success */
	return TRUE;
}

static void
fu_linux_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuLinuxDevice *self = FU_LINUX_DEVICE(device);
	FuLinuxDevicePrivate *priv = GET_PRIVATE(self);
	//	fwupd_codec_string_append(str, idt, "SysfsPath", priv->sysfs_path);
	fwupd_codec_string_append(str, idt, "Subsystem", priv->subsystem);
	fwupd_codec_string_append(str, idt, "Driver", priv->driver);
	fwupd_codec_string_append(str, idt, "BindId", priv->bind_id);
	fwupd_codec_string_append(str, idt, "DeviceFile", priv->device_file);
	fwupd_codec_string_append_hex(str, idt, "Vendor", priv->vendor);
	fwupd_codec_string_append_hex(str, idt, "Model", priv->model);
	fwupd_codec_string_append_hex(str, idt, "SubsystemVendor", priv->subsystem_vendor);
	fwupd_codec_string_append_hex(str, idt, "SubsystemModel", priv->subsystem_model);
	fwupd_codec_string_append_hex(str, idt, "PciClass", priv->pci_class);
	fwupd_codec_string_append_hex(str, idt, "Revision", priv->revision);
}

static void
fu_linux_device_incorporate(FuDevice *self, FuDevice *donor)
{
	FuLinuxDevice *uself = FU_LINUX_DEVICE(self);
	FuLinuxDevice *udonor = FU_LINUX_DEVICE(donor);
	FuLinuxDevicePrivate *priv = GET_PRIVATE(uself);

	g_return_if_fail(FU_IS_LINUX_DEVICE(self));
	g_return_if_fail(FU_IS_LINUX_DEVICE(donor));

	//	if (priv->sysfs_path == NULL)
	//		fu_linux_device_set_sysfs_path(uself,
	// fu_linux_device_get_sysfs_path(udonor));
	if (priv->device_file == NULL)
		fu_linux_device_set_device_file(uself, fu_linux_device_get_device_file(udonor));
	if (priv->subsystem == NULL)
		fu_linux_device_set_subsystem(uself, fu_linux_device_get_subsystem(udonor));
	if (priv->bind_id == NULL)
		fu_linux_device_set_bind_id(uself, fu_linux_device_get_bind_id(udonor));
	if (priv->driver == NULL)
		fu_linux_device_set_driver(uself, fu_linux_device_get_driver(udonor));
	//	if (priv->io_channel == NULL)
	//		fu_linux_device_set_io_channel(uself,
	// fu_linux_device_get_io_channel(udonor));
	if (priv->vendor == 0x0)
		fu_linux_device_set_vendor(uself, fu_linux_device_get_vendor(udonor));
	if (priv->model == 0x0)
		fu_linux_device_set_model(uself, fu_linux_device_get_model(udonor));
	if (priv->subsystem_vendor == 0x0)
		fu_linux_device_set_subsystem_vendor(uself,
						     fu_linux_device_get_subsystem_vendor(udonor));
	if (priv->subsystem_model == 0x0)
		fu_linux_device_set_subsystem_model(uself,
						    fu_linux_device_get_subsystem_model(udonor));
	if (priv->revision == 0x0)
		fu_linux_device_set_revision(uself, fu_linux_device_get_revision(udonor));
}

static void
fu_linux_device_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_json = fu_linux_device_add_json;
	iface->from_json = fu_linux_device_from_json;
}

static void
fu_linux_device_init(FuLinuxDevice *self)
{
}

static void
fu_linux_device_class_init(FuLinuxDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fu_linux_device_finalize;
	object_class->get_property = fu_linux_device_get_property;
	object_class->set_property = fu_linux_device_set_property;
	device_class->probe = fu_linux_device_probe;
	device_class->to_string = fu_linux_device_to_string;
	device_class->incorporate = fu_linux_device_incorporate;
	device_class->open = fu_linux_device_open;
	device_class->close = fu_linux_device_close;
	device_class->bind_driver = fu_linux_device_bind_driver;
	device_class->unbind_driver = fu_linux_device_unbind_driver;
	device_class->dump_firmware = fu_linux_device_dump_firmware;

	/**
	 * FuLinuxDevice:sysfs-path:
	 *
	 * The sysfs path.
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_string("sysfs-path",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_SYSFS_PATH, pspec);

	/**
	 * FuLinuxDevice:subsystem:
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
	 * FuLinuxDevice:bind-id:
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
	 * FuLinuxDevice:driver:
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
	 * FuLinuxDevice:device-file:
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
	 * FuLinuxDevice:devtype:
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
