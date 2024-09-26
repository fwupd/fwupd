/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

#include "fu-bytes.h"
#include "fu-dump.h"
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
	gchar *uuid;
	gchar *parent_device_file;
} FuMeiDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuMeiDevice, fu_mei_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_mei_device_get_instance_private(o))

static void
fu_mei_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuMeiDevice *self = FU_MEI_DEVICE(device);
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, "Uuid", priv->uuid);
	fwupd_codec_string_append(str, idt, "ParentDeviceFile", priv->parent_device_file);
	fwupd_codec_string_append_hex(str, idt, "MaxMsgLength", priv->max_msg_length);
	fwupd_codec_string_append_hex(str, idt, "ProtocolVer", priv->protocol_version);
}

static gboolean
fu_mei_device_ensure_parent_device_file(FuMeiDevice *self, GError **error)
{
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *fn;
	g_autofree gchar *parent_tmp = NULL;
	g_autofree gchar *parent_mei_path = NULL;
	g_autoptr(FuUdevDevice) parent = NULL;
	g_autoptr(GDir) dir = NULL;

	/* get direct parent */
	parent = FU_UDEV_DEVICE(
	    fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), "pci", error));
	if (parent == NULL)
		return FALSE;

	/* look for the only child with this subsystem */
	parent_mei_path = g_build_filename(fu_udev_device_get_sysfs_path(parent), "mei", NULL);
	dir = g_dir_open(parent_mei_path, 0, NULL);
	if (dir == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no MEI parent dir for %s",
			    fu_udev_device_get_sysfs_path(parent));
		return FALSE;
	}
	fn = g_dir_read_name(dir);
	if (fn == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no MEI parent in %s",
			    parent_mei_path);
		return FALSE;
	}

	/* success */
	parent_tmp = g_build_filename(fu_udev_device_get_sysfs_path(parent), "mei", fn, NULL);
	if (g_strcmp0(parent_tmp, priv->parent_device_file) != 0) {
		g_free(priv->parent_device_file);
		priv->parent_device_file = g_steal_pointer(&parent_tmp);
	}
	return TRUE;
}

static void
fu_mei_device_set_uuid(FuMeiDevice *self, const gchar *uuid)
{
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	if (g_strcmp0(priv->uuid, uuid) == 0)
		return;
	g_free(priv->uuid);
	priv->uuid = g_strdup(uuid);
}

static gboolean
fu_mei_device_pci_probe(FuMeiDevice *self, GError **error)
{
	g_autoptr(FuDevice) pci_donor = NULL;

	pci_donor = fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), "pci", error);
	if (pci_donor == NULL)
		return FALSE;
	if (!fu_device_probe(pci_donor, error))
		return FALSE;
	fu_device_incorporate(FU_DEVICE(self),
			      pci_donor,
			      FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
				  FU_DEVICE_INCORPORATE_FLAG_VID | FU_DEVICE_INCORPORATE_FLAG_PID |
				  FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);

	/* success */
	return TRUE;
}

static gboolean
fu_mei_device_probe(FuDevice *device, GError **error)
{
	FuMeiDevice *self = FU_MEI_DEVICE(device);
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *uuid = NULL;
	g_autoptr(GError) error_local = NULL;

	/* copy the PCI-specific vendor */
	if (!fu_mei_device_pci_probe(self, error))
		return FALSE;

	/* this has to exist */
	uuid = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					 "uuid",
					 FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					 &error_local);
	if (uuid == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "UUID not provided: %s",
			    error_local->message);
		return FALSE;
	}
	fu_mei_device_set_uuid(self, uuid);
	fu_device_add_guid(device, uuid);

	/* get the mei[0-9] device file the parent is using */
	if (!fu_mei_device_ensure_parent_device_file(self, error))
		return FALSE;

	/* the kernel is missing `dev` on mei_me children */
	if (fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)) == NULL) {
		g_autofree gchar *basename = g_path_get_basename(priv->parent_device_file);
		g_autofree gchar *device_file = g_build_filename("/dev", basename, NULL);
		fu_udev_device_set_device_file(FU_UDEV_DEVICE(device), device_file);
	}

	/* success */
	return TRUE;
}

static gchar *
fu_mei_device_get_parent_attr(FuMeiDevice *self, const gchar *basename, guint idx, GError **error)
{
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *fn = NULL;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* sanity check */
	if (priv->parent_device_file == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no parent device file");
		return NULL;
	}

	/* load lines */
	fn = g_build_filename(priv->parent_device_file, basename, NULL);
	blob = fu_bytes_get_contents(fn, error);
	if (blob == NULL)
		return NULL;
	lines = fu_strsplit((const gchar *)g_bytes_get_data(blob, NULL),
			    g_bytes_get_size(blob),
			    "\n",
			    -1);
	if (g_strv_length(lines) <= idx) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "requested line %u of %u",
			    idx,
			    g_strv_length(lines));
		return NULL;
	}

	/* success */
	return g_strdup(lines[idx]);
}

/**
 * fu_mei_device_get_fw_ver:
 * @self: a #FuMeiDevice
 * @idx: line index
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware version for a specific index.
 *
 * Returns: string value
 *
 * Since: 1.8.7
 **/
gchar *
fu_mei_device_get_fw_ver(FuMeiDevice *self, guint idx, GError **error)
{
	g_return_val_if_fail(FU_IS_MEI_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fu_mei_device_get_parent_attr(self, "fw_ver", idx, error);
}

/**
 * fu_mei_device_get_fw_status:
 * @self: a #FuMeiDevice
 * @idx: line index
 * @error: (nullable): optional return location for an error
 *
 * Gets the firmware status for a specific index.
 *
 * Returns: string value
 *
 * Since: 1.8.7
 **/
gchar *
fu_mei_device_get_fw_status(FuMeiDevice *self, guint idx, GError **error)
{
	g_return_val_if_fail(FU_IS_MEI_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fu_mei_device_get_parent_attr(self, "fw_status", idx, error);
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
 * @req_protocol_version: required protocol version, or 0
 * @error: (nullable): optional return location for an error
 *
 * Connects to the MEI device.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_mei_device_connect(FuMeiDevice *self, guchar req_protocol_version, GError **error)
{
#ifdef HAVE_MEI_H
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_guid_t guid_le = {0x0};
	struct mei_client *cl;
	struct mei_connect_client_data data = {0x0};

	g_return_val_if_fail(FU_IS_MEI_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fwupd_guid_from_string(priv->uuid, &guid_le, FWUPD_GUID_FLAG_MIXED_ENDIAN, error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "guid_le", (guint8 *)&guid_le, sizeof(guid_le));
	memcpy(&data.in_client_uuid, &guid_le, sizeof(guid_le)); /* nocheck:blocked */
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  IOCTL_MEI_CONNECT_CLIENT,
				  (guint8 *)&data,
				  sizeof(data),
				  NULL, /* rc */
				  FU_MEI_DEVICE_IOCTL_TIMEOUT,
				  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
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
	FuIOChannel *io_channel = fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self));

	g_return_val_if_fail(FU_IS_MEI_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	rc = read(fu_io_channel_unix_get_fd(io_channel), buf, bufsz);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "read failed %u: %s",
			    (guint)rc,
			    g_strerror(errno));
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, "read", buf, rc);
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
	FuIOChannel *io_channel = fu_udev_device_get_io_channel(FU_UDEV_DEVICE(self));
	guint fd = fu_io_channel_unix_get_fd(io_channel);

	g_return_val_if_fail(FU_IS_MEI_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;

	fu_dump_raw(G_LOG_DOMAIN, "write", buf, bufsz);
	written = write(fd, buf, bufsz);
	if (written < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "write failed with status %" G_GSSIZE_FORMAT " %s",
			    written,
			    g_strerror(errno));
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
		    "write failed on select with status %" G_GSSIZE_FORMAT,
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

	/* copy private instance data */
	priv->max_msg_length = priv_donor->max_msg_length;
	priv->protocol_version = priv_donor->protocol_version;
	if (priv->uuid == NULL)
		fu_mei_device_set_uuid(self, priv_donor->uuid);
	if (priv->parent_device_file == NULL)
		priv->parent_device_file = g_strdup(priv_donor->parent_device_file);
}

static void
fu_mei_device_init(FuMeiDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

static void
fu_mei_device_finalize(GObject *object)
{
	FuMeiDevice *self = FU_MEI_DEVICE(object);
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	g_free(priv->uuid);
	g_free(priv->parent_device_file);
	G_OBJECT_CLASS(fu_mei_device_parent_class)->finalize(object);
}

static void
fu_mei_device_class_init(FuMeiDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_mei_device_finalize;
	device_class->probe = fu_mei_device_probe;
	device_class->to_string = fu_mei_device_to_string;
	device_class->incorporate = fu_mei_device_incorporate;
}
