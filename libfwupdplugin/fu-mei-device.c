/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuMeiDevice"

#include "config.h"

#include <fcntl.h>

#ifdef HAVE_MEI_H
#include <linux/mei.h>
#endif
#ifdef HAVE_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_SELECT_H
#include <sys/select.h>
#endif

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fu-mei-device.h"
#include "fu-string.h"

#define FU_MEI_DEVICE_IOCTL_TIMEOUT 5000 /* ms */

/**
 * FuMeiDevice
 *
 * The Intel proprietary Management Engine Interface.
 *
 * See also: #FuUdevDevice
 */

typedef struct {
	guint32 max_msg_length;
	guint8 protocol_version;
} FuMeiDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuMeiDevice, fu_mei_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_mei_device_get_instance_private(o))

static void
fu_mei_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuMeiDevice *self = FU_MEI_DEVICE(device);
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	FU_DEVICE_CLASS(fu_mei_device_parent_class)->to_string(device, idt, str);
	if (priv->max_msg_length > 0x0)
		fu_string_append_kx(str, idt, "MaxMsgLength", priv->max_msg_length);
	if (priv->protocol_version > 0x0)
		fu_string_append_kx(str, idt, "ProtocolVer", priv->protocol_version);
}

static gboolean
fu_mei_device_probe(FuDevice *device, GError **error)
{
	/* only open the main device */
	if (fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)) == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no device");
		return FALSE;
	}

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_mei_device_parent_class)->probe(device, error))
		return FALSE;

	/* set the physical ID */
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "pci", error);
}

/**
 * fu_mei_device_get_max_msg_length:
 * @self: a #FuMeiDevice
 *
 * Gets the maximum message length.
 *
 * Returns: integer
 *
 * Since: 1.8.2
 **/
guint32
fu_mei_device_get_max_msg_length(FuMeiDevice *self)
{
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_MEI_DEVICE(self), G_MAXUINT32);
	return priv->max_msg_length;
}

/**
 * fu_mei_device_get_protocol_version:
 * @self: a #FuMeiDevice
 *
 * Gets the protocol version, or 0x for unset.
 *
 * Returns: integer
 *
 * Since: 1.8.2
 **/
guint8
fu_mei_device_get_protocol_version(FuMeiDevice *self)
{
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_MEI_DEVICE(self), G_MAXUINT8);
	return priv->protocol_version;
}

/**
 * fu_mei_device_connect:
 * @self: a #FuMeiDevice
 * @guid: A GUID, e.g. "2800f812-b7b4-2d4b-aca8-46e0ff65814c"
 * @req_protocol_version: required protocol version, or 0
 * @error: (nullable): optional return location for an error
 *
 * Connects to the MEI device for a specific GUID.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_mei_device_connect(FuMeiDevice *self,
		      const gchar *guid,
		      guchar req_protocol_version,
		      GError **error)
{
#ifdef HAVE_MEI_H
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_guid_t guid_le = {0x0};
	struct mei_client *cl;
	struct mei_connect_client_data data = {0x0};

	g_return_val_if_fail(FU_IS_MEI_DEVICE(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fwupd_guid_from_string(guid, &guid_le, FWUPD_GUID_FLAG_NONE, error))
		return FALSE;
	memcpy(&data.in_client_uuid, &guid_le, sizeof(guid_le));
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  IOCTL_MEI_CONNECT_CLIENT,
				  (guint8 *)&data,
				  NULL, /* rc */
				  FU_MEI_DEVICE_IOCTL_TIMEOUT,
				  error))
		return FALSE;

	cl = &data.out_client_properties;
	if (req_protocol_version > 0 && cl->protocol_version != req_protocol_version) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Intel MEI protocol version not supported %i",
			    cl->protocol_version);
		return FALSE;
	}

	/* success */
	priv->max_msg_length = cl->max_msg_length;
	priv->protocol_version = cl->protocol_version;
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "linux/mei.h not supported");
	return FALSE;
#endif
}

/**
 * fu_mei_device_read:
 * @self: a #FuMeiDevice
 * @buf: (out): data
 * @bufsz: size of @data
 * @bytes_read: (nullable): bytes read
 * @timeout_ms: timeout
 * @error: (nullable): optional return location for an error
 *
 * Read raw bytes from the device.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_mei_device_read(FuMeiDevice *self,
		   guint8 *buf,
		   gsize bufsz,
		   gsize *bytes_read,
		   guint timeout_ms,
		   GError **error)
{
	gssize rc;

	g_return_val_if_fail(FU_IS_MEI_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	rc = read(fu_udev_device_get_fd(FU_UDEV_DEVICE(self)), buf, bufsz);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "read failed %u: %s",
			    (guint)rc,
			    strerror(errno));
		return FALSE;
	}
	if (bytes_read != NULL)
		*bytes_read = (gsize)rc;
	return TRUE;
}

/**
 * fu_mei_device_write:
 * @self: a #FuMeiDevice
 * @buf: (out): data
 * @bufsz: size of @data
 * @timeout_ms: timeout
 * @error: (nullable): optional return location for an error
 *
 * Write raw bytes to the device.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_mei_device_write(FuMeiDevice *self,
		    const guint8 *buf,
		    gsize bufsz,
		    guint timeout_ms,
		    GError **error)
{
#ifdef HAVE_SELECT_H
	struct timeval tv;
	gssize written;
	gssize rc;
	fd_set set;
	guint fd = fu_udev_device_get_fd(FU_UDEV_DEVICE(self));

	g_return_val_if_fail(FU_IS_MEI_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000000;

	written = write(fd, buf, bufsz);
	if (written < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "write failed with status %zd %s",
			    written,
			    strerror(errno));
		return FALSE;
	}
	if ((gsize)written != bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "only wrote %" G_GSSIZE_FORMAT " of %" G_GSIZE_FORMAT,
			    written,
			    bufsz);
		return FALSE;
	}

	FD_ZERO(&set);
	FD_SET(fd, &set);
	rc = select(fd + 1, &set, NULL, NULL, &tv);
	if (rc > 0 && FD_ISSET(fd, &set))
		return TRUE;

	/* timed out */
	if (rc == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "write failed on timeout with status");
		return FALSE;
	}

	/* rc < 0 */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_WRITE,
		    "write failed on select with status %zd",
		    rc);
	return FALSE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "linux/select.h not supported");
	return FALSE;
#endif
}

static void
fu_mei_device_incorporate(FuDevice *device, FuDevice *donor)
{
	FuMeiDevice *self = FU_MEI_DEVICE(device);
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	FuMeiDevicePrivate *priv_donor = GET_PRIVATE(FU_MEI_DEVICE(donor));

	g_return_if_fail(FU_IS_MEI_DEVICE(self));
	g_return_if_fail(FU_IS_MEI_DEVICE(donor));

	/* FuUdevDevice->incorporate */
	FU_DEVICE_CLASS(fu_mei_device_parent_class)->incorporate(device, donor);

	/* copy private instance data */
	priv->max_msg_length = priv_donor->max_msg_length;
	priv->protocol_version = priv_donor->protocol_version;
}

static void
fu_mei_device_init(FuMeiDevice *self)
{
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
				 FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
				     FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT);
}

static void
fu_mei_device_class_init(FuMeiDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_mei_device_probe;
	klass_device->to_string = fu_mei_device_to_string;
	klass_device->incorporate = fu_mei_device_incorporate;
}
