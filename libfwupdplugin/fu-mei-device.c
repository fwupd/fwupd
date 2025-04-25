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
} FuMeiDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuMeiDevice, fu_mei_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_mei_device_get_instance_private(o))

static void
fu_mei_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuMeiDevice *self = FU_MEI_DEVICE(device);
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, "Uuid", priv->uuid);
	fwupd_codec_string_append_hex(str, idt, "MaxMsgLength", priv->max_msg_length);
	fwupd_codec_string_append_hex(str, idt, "ProtocolVer", priv->protocol_version);
}

static gboolean
fu_mei_device_set_uuid(FuMeiDevice *self, const gchar *uuid)
{
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	if (g_strcmp0(priv->uuid, uuid) == 0)
		return FALSE;
	g_free(priv->uuid);
	priv->uuid = g_strdup(uuid);
	return TRUE;
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
				  FU_DEVICE_INCORPORATE_FLAG_INSTANCE_KEYS |
				  FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);

	/* success */
	return TRUE;
}

static gboolean
fu_mei_device_interfaces_probe(FuMeiDevice *self, GError **error)
{
	gsize prefixlen;
	g_autofree gchar *prefix = NULL;
	g_autoptr(FuDevice) parent = NULL;
	g_autoptr(GPtrArray) attrs = NULL;

	/* all the interfaces are prefixed by the parent basename */
	parent = fu_device_get_backend_parent(FU_DEVICE(self), error);
	if (parent == NULL)
		return FALSE;
	if (fu_device_get_backend_id(parent) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no parent backend-id");
		return FALSE;
	}
	prefix = g_path_get_basename(fu_device_get_backend_id(parent));
	prefixlen = strlen(prefix);

	/* add any instance IDs that match */
	attrs = fu_udev_device_list_sysfs(FU_UDEV_DEVICE(parent), error);
	if (attrs == NULL)
		return FALSE;
	for (guint i = 0; i < attrs->len; i++) {
		const gchar *attr = g_ptr_array_index(attrs, i);
		if (g_str_has_prefix(attr, prefix)) {
			fu_device_add_instance_id_full(FU_DEVICE(self),
						       attr + prefixlen + 1,
						       FU_DEVICE_INSTANCE_FLAG_QUIRKS);
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mei_device_probe(FuDevice *device, GError **error)
{
	FuMeiDevice *self = FU_MEI_DEVICE(device);

	/* copy the PCI-specific vendor */
	if (!fu_mei_device_pci_probe(self, error))
		return FALSE;

	/* add interfaces */
	if (!fu_mei_device_interfaces_probe(self, error))
		return FALSE;

	/* for quirk matches */
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "PCI",
					 "VEN",
					 NULL);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "PCI",
					 "VEN",
					 "DEV",
					 NULL);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "PCI",
					 "DRIVER",
					 NULL);

	/* success */
	return TRUE;
}

static gchar *
fu_mei_device_get_multiline_attr(FuMeiDevice *self, const gchar *attr, guint idx, GError **error)
{
	g_auto(GStrv) lines = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* load lines */
	blob = fu_udev_device_read_sysfs_bytes(FU_UDEV_DEVICE(self), attr, -1, 500, error);
	if (blob == NULL)
		return NULL;
	lines = fu_strsplit_bytes(blob, "\n", -1);
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
	return fu_mei_device_get_multiline_attr(self, "fw_ver", idx, error);
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
	return fu_mei_device_get_multiline_attr(self, "fw_status", idx, error);
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
 * @uuid: interface UUID
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
fu_mei_device_connect(FuMeiDevice *self,
		      const gchar *uuid,
		      guint8 req_protocol_version,
		      GError **error)
{
#ifdef HAVE_MEI_H
	FuMeiDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_guid_t guid_le = {0x0};
	struct mei_client *cl;
	struct mei_connect_client_data data = {0x0};
	g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));

	g_return_val_if_fail(FU_IS_MEI_DEVICE(self), FALSE);
	g_return_val_if_fail(uuid != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* already using this UUID */
	if (!fu_mei_device_set_uuid(self, uuid))
		return TRUE;

	if (!fwupd_guid_from_string(priv->uuid, &guid_le, FWUPD_GUID_FLAG_MIXED_ENDIAN, error))
		return FALSE;
	memcpy(&data.in_client_uuid, &guid_le, sizeof(guid_le)); /* nocheck:blocked */
	g_debug("connecting to %s", priv->uuid);
	if (!fu_ioctl_execute(ioctl,
			      IOCTL_MEI_CONNECT_CLIENT,
			      (guint8 *)&data,
			      sizeof(data),
			      NULL, /* rc */
			      FU_MEI_DEVICE_IOCTL_TIMEOUT,
			      FU_IOCTL_FLAG_NONE,
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
	g_return_val_if_fail(FU_IS_MEI_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_udev_device_read(FU_UDEV_DEVICE(self),
				   buf,
				   bufsz,
				   bytes_read,
				   timeout_ms,
				   FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
				   error);
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
	g_return_val_if_fail(FU_IS_MEI_DEVICE(self), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_udev_device_write(FU_UDEV_DEVICE(self),
				    buf,
				    bufsz,
				    timeout_ms,
				    FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
				    error);
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
