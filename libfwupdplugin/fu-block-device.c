/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuBlockDevice"

#include "config.h"

#include "fu-block-device.h"
#include "fu-common.h"
#include "fu-io-channel.h"

/**
 * FuBlockDevice:
 *
 * A block device, typically a FAT32 volume
 *
 * See also: [class@FuDevice]
 */

typedef struct {
	gchar *uuid;
	gchar *label;
	gchar *filename;
} FuBlockDevicePrivate;

enum { PROP_0, PROP_UUID, PROP_LABEL, PROP_FILENAME, PROP_LAST };

G_DEFINE_TYPE_WITH_PRIVATE(FuBlockDevice, fu_block_device, FU_TYPE_DEVICE)

#define GET_PRIVATE(o) (fu_block_device_get_instance_private(o))

static void
fu_block_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuBlockDevice *self = FU_BLOCK_DEVICE(device);
	FuBlockDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->uuid != NULL)
		fu_common_string_append_kv(str, idt, "Uuid", priv->uuid);
	if (priv->label != NULL)
		fu_common_string_append_kv(str, idt, "Label", priv->label);
	if (priv->filename != NULL)
		fu_common_string_append_kv(str, idt, "Filename", priv->filename);
}

static gboolean
fu_block_device_probe(FuDevice *device, GError **error)
{
	FuBlockDevice *self = FU_BLOCK_DEVICE(device);
	FuBlockDevicePrivate *priv = GET_PRIVATE(self);

	if (priv->uuid != NULL && priv->label != NULL) {
		g_autofree gchar *instance_id0 = NULL;
		instance_id0 = g_strdup_printf("BLOCK\\UUID_%s&LABEL_%s", priv->uuid, priv->label);
		fu_device_add_instance_id_full(FU_DEVICE(self),
					       instance_id0,
					       FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}
	if (priv->uuid != NULL) {
		g_autofree gchar *instance_id0 = NULL;
		instance_id0 = g_strdup_printf("BLOCK\\UUID_%s", priv->uuid);
		fu_device_add_instance_id_full(FU_DEVICE(self),
					       instance_id0,
					       FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}
	if (priv->label != NULL) {
		g_autofree gchar *instance_id0 = NULL;
		instance_id0 = g_strdup_printf("BLOCK\\LABEL_%s", priv->label);
		fu_device_add_instance_id_full(FU_DEVICE(self),
					       instance_id0,
					       FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}

	/* success */
	return TRUE;
}

/**
 * fu_block_device_set_uuid:
 * @self: a #FuBlockDevice
 * @uuid: the uuid, e.g. `E478-FA50`
 *
 * Sets the UUID.
 *
 * Since: 1.7.4
 **/
void
fu_block_device_set_uuid(FuBlockDevice *self, const gchar *uuid)
{
	FuBlockDevicePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_BLOCK_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->uuid, uuid) == 0)
		return;

	g_free(priv->uuid);
	priv->uuid = g_strdup(uuid);
	g_object_notify(G_OBJECT(self), "uuid");
}

/**
 * fu_block_device_get_uuid:
 * @self: a #FuBlockDevice
 *
 * Gets the UUID.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 1.7.4
 **/
const gchar *
fu_block_device_get_uuid(FuBlockDevice *self)
{
	FuBlockDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_BLOCK_DEVICE(self), NULL);
	return priv->uuid;
}

/**
 * fu_block_device_set_label:
 * @self: a #FuBlockDevice
 * @label: the label, e.g. `FWUPDATE`
 *
 * Sets the block device label.
 *
 * Since: 1.7.4
 **/
void
fu_block_device_set_label(FuBlockDevice *self, const gchar *label)
{
	FuBlockDevicePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_BLOCK_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->label, label) == 0)
		return;

	g_free(priv->label);
	priv->label = g_strdup(label);
	g_object_notify(G_OBJECT(self), "label");
}

/**
 * fu_block_device_get_label:
 * @self: a #FuBlockDevice
 *
 * Gets the label.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 1.7.4
 **/
const gchar *
fu_block_device_get_label(FuBlockDevice *self)
{
	FuBlockDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_BLOCK_DEVICE(self), NULL);
	return priv->label;
}

/**
 * fu_block_device_set_filename:
 * @self: a #FuBlockDevice
 * @filename: the filename, e.g. `FWUPDATE`
 *
 * Sets the filename to write to the volume.
 *
 * Since: 1.7.4
 **/
void
fu_block_device_set_filename(FuBlockDevice *self, const gchar *filename)
{
	FuBlockDevicePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_BLOCK_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->filename, filename) == 0)
		return;

	g_free(priv->filename);
	priv->filename = g_strdup(filename);
	g_object_notify(G_OBJECT(self), "filename");
}

/**
 * fu_block_device_get_filename:
 * @self: a #FuBlockDevice
 *
 * Gets the filename to write to the volume.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 1.7.4
 **/
const gchar *
fu_block_device_get_filename(FuBlockDevice *self)
{
	FuBlockDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_BLOCK_DEVICE(self), NULL);
	return priv->filename;
}

static gboolean
fu_block_device_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuBlockDevice *self = FU_BLOCK_DEVICE(device);
	FuBlockDevicePrivate *priv = GET_PRIVATE(self);

	if (g_strcmp0(key, "BlockDeviceFilename") == 0) {
		priv->filename = g_strdup(value);
		return TRUE;
	}
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no supported");
	return FALSE;
}

static gchar *
fu_block_device_get_full_path(FuBlockDevice *self, GError **error)
{
	FuBlockDevicePrivate *priv = GET_PRIVATE(self);

	/* sanity check */
	if (fu_device_get_logical_id(FU_DEVICE(self)) == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "no valid path: no logical ID");
		return NULL;
	}
	if (priv->filename == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "no valid path: no target filename");
		return NULL;
	}
	return g_build_filename(fu_device_get_logical_id(FU_DEVICE(self)), priv->filename, NULL);
}

static gboolean
fu_block_device_write_firmware(FuDevice *device,
			       FuFirmware *firmware,
			       FuProgress *progress,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuBlockDevice *self = FU_BLOCK_DEVICE(device);
	gssize wrote;
	g_autofree gchar *fn = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GOutputStream) ostr = NULL;

	/* get blob */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* open file for writing; no cleverness */
	fn = fu_block_device_get_full_path(self, error);
	if (fn == NULL)
		return FALSE;
	file = g_file_new_for_path(fn);
	ostr = G_OUTPUT_STREAM(g_file_replace(file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error));
	if (ostr == NULL)
		return FALSE;

	/* write in one chunk and let the kernel do the right thing :) */
	wrote = g_output_stream_write(ostr,
				      g_bytes_get_data(fw, NULL),
				      g_bytes_get_size(fw),
				      NULL,
				      error);
	if (wrote < 0)
		return FALSE;
	if ((gsize)wrote != g_bytes_get_size(fw)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "only wrote 0x%x bytes",
			    (guint)wrote);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_block_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuBlockDevice *self = FU_BLOCK_DEVICE(device);
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GInputStream) istr = NULL;

	/* open for reading */
	fn = fu_block_device_get_full_path(self, error);
	if (fn == NULL)
		return NULL;
	file = g_file_new_for_path(fn);
	istr = G_INPUT_STREAM(g_file_read(file, NULL, error));
	if (istr == NULL)
		return NULL;

	/* read all in one big chunk */
	return fu_common_get_contents_stream(istr, G_MAXUINT32, error);
}

static void
fu_block_device_incorporate(FuDevice *self, FuDevice *donor)
{
	FuBlockDevice *uself = FU_BLOCK_DEVICE(self);
	FuBlockDevice *udonor = FU_BLOCK_DEVICE(donor);
	FuBlockDevicePrivate *priv = GET_PRIVATE(uself);
	FuBlockDevicePrivate *privdonor = GET_PRIVATE(udonor);

	if (priv->uuid == NULL)
		fu_block_device_set_uuid(uself, privdonor->uuid);
	if (priv->label == NULL)
		fu_block_device_set_label(uself, privdonor->label);
	if (priv->filename == NULL)
		fu_block_device_set_filename(uself, privdonor->filename);
}

static void
fu_block_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuBlockDevice *self = FU_BLOCK_DEVICE(object);
	FuBlockDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_UUID:
		g_value_set_string(value, priv->uuid);
		break;
	case PROP_LABEL:
		g_value_set_string(value, priv->label);
		break;
	case PROP_FILENAME:
		g_value_set_string(value, priv->filename);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_block_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuBlockDevice *self = FU_BLOCK_DEVICE(object);
	switch (prop_id) {
	case PROP_UUID:
		fu_block_device_set_uuid(self, g_value_get_string(value));
		break;
	case PROP_LABEL:
		fu_block_device_set_label(self, g_value_get_string(value));
		break;
	case PROP_FILENAME:
		fu_block_device_set_filename(self, g_value_get_string(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_block_device_finalize(GObject *object)
{
	FuBlockDevice *self = FU_BLOCK_DEVICE(object);
	FuBlockDevicePrivate *priv = GET_PRIVATE(self);

	g_free(priv->uuid);
	g_free(priv->label);
	g_free(priv->filename);
	G_OBJECT_CLASS(fu_block_device_parent_class)->finalize(object);
}

static void
fu_block_device_init(FuBlockDevice *self)
{
}

static void
fu_block_device_class_init(FuBlockDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->get_property = fu_block_device_get_property;
	object_class->set_property = fu_block_device_set_property;
	object_class->finalize = fu_block_device_finalize;
	device_class->probe = fu_block_device_probe;
	device_class->to_string = fu_block_device_to_string;
	device_class->incorporate = fu_block_device_incorporate;
	device_class->set_quirk_kv = fu_block_device_set_quirk_kv;
	device_class->write_firmware = fu_block_device_write_firmware;
	device_class->dump_firmware = fu_block_device_dump_firmware;

	pspec =
	    g_param_spec_string("uuid", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_UUID, pspec);

	pspec =
	    g_param_spec_string("label", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_LABEL, pspec);

	pspec = g_param_spec_string("filename",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FILENAME, pspec);
}
