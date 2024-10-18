/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBlockPartition"

#include "config.h"

#ifdef HAVE_BLKID
#include <blkid.h>
#endif

#include "fu-block-partition.h"
#include "fu-string.h"

/**
 * FuBlockPartition
 *
 * See also: #FuBlockDevice
 */

typedef struct {
	gchar *fs_type;
	gchar *fs_uuid;
} FuBlockPartitionPrivate;

#define GET_PRIVATE(o) (fu_block_partition_get_instance_private(o))

G_DEFINE_TYPE_WITH_PRIVATE(FuBlockPartition, fu_block_partition, FU_TYPE_BLOCK_DEVICE)

#ifdef HAVE_BLKID
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(blkid_probe, blkid_free_probe, NULL)
#endif

static void
fu_block_partition_to_string(FuDevice *device, guint idt, GString *str)
{
	FuBlockPartition *self = FU_BLOCK_PARTITION(device);
	FuBlockPartitionPrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, "FsType", priv->fs_type);
	fwupd_codec_string_append(str, idt, "FsUuid", priv->fs_uuid);
}

#ifdef HAVE_BLKID
static void
fu_block_partition_set_fs_type(FuBlockPartition *self, const gchar *fs_type, gsize fs_typelen)
{
	FuBlockPartitionPrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->fs_type, fs_type) == 0)
		return;

	g_free(priv->fs_type);
	priv->fs_type = fu_strsafe(fs_type, fs_typelen);
}

static void
fu_block_partition_set_fs_uuid(FuBlockPartition *self, const gchar *fs_uuid, gsize fs_uuidlen)
{
	FuBlockPartitionPrivate *priv = GET_PRIVATE(self);

	/* not changed */
	if (g_strcmp0(priv->fs_uuid, fs_uuid) == 0)
		return;

	g_free(priv->fs_uuid);
	priv->fs_uuid = fu_strsafe(fs_uuid, fs_uuidlen);
}
#endif

static void
fu_block_partition_incorporate(FuDevice *self, FuDevice *donor)
{
	FuBlockPartition *uself = FU_BLOCK_PARTITION(self);
	FuBlockPartition *udonor = FU_BLOCK_PARTITION(donor);
	FuBlockPartitionPrivate *priv = GET_PRIVATE(uself);

	g_return_if_fail(FU_IS_BLOCK_PARTITION(self));
	g_return_if_fail(FU_IS_BLOCK_PARTITION(donor));

	if (priv->fs_type == NULL)
		priv->fs_type = g_strdup(fu_block_partition_get_fs_type(udonor));
	if (priv->fs_uuid == NULL)
		priv->fs_uuid = g_strdup(fu_block_partition_get_fs_uuid(udonor));
}

/**
 * fu_block_partition_get_fs_type:
 * @self: a #FuBlockPartition
 *
 * Returns the filesystem type, e.g. `msdos`.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 2.0.2
 **/
const gchar *
fu_block_partition_get_fs_type(FuBlockPartition *self)
{
	FuBlockPartitionPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_BLOCK_PARTITION(self), NULL);
	return priv->fs_type;
}

/**
 * fu_block_partition_get_fs_uuid:
 * @self: a #FuBlockPartition
 *
 * Returns the filesystem UUID.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 2.0.2
 **/
const gchar *
fu_block_partition_get_fs_uuid(FuBlockPartition *self)
{
	FuBlockPartitionPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_BLOCK_PARTITION(self), NULL);
	return priv->fs_uuid;
}

static gboolean
fu_block_partition_setup(FuDevice *device, GError **error)
{
	FuBlockPartition *self = FU_BLOCK_PARTITION(device);
	FuBlockPartitionPrivate *priv = GET_PRIVATE(self);
	FuDeviceEvent *event = NULL;
	FuIOChannel *io_channel = fu_udev_device_get_io_channel(FU_UDEV_DEVICE(device));
	g_autofree gchar *event_id = NULL;
#ifdef HAVE_BLKID
	gint rc;
	const gchar *data;
	gsize datalen = 0;
	g_auto(blkid_probe) pr = NULL;
#endif

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup_printf("FuBlockPartitionSetup:DeviceFile=%s",
					   fu_udev_device_get_device_file(FU_UDEV_DEVICE(self)));
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return FALSE;
		priv->fs_type = g_strdup(fu_device_event_get_str(event, "FsType", NULL));
		priv->fs_uuid = g_strdup(fu_device_event_get_str(event, "FsUuid", NULL));
		return TRUE;
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	/* sanity check */
	if (io_channel == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no device");
		return FALSE;
	}

#ifdef HAVE_BLKID
	/* probe */
	pr = blkid_new_probe();
	if (pr == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to create blkid prober");
		return FALSE;
	}
	blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_UUID | BLKID_SUBLKS_TYPE);
	rc = blkid_probe_set_device(pr, fu_io_channel_unix_get_fd(io_channel), 0x0, 0x0);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to setup device: %i",
			    rc);
		return FALSE;
	}
	rc = blkid_do_safeprobe(pr);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to probe device: %i",
			    rc);
		return FALSE;
	}
	if (g_getenv("FWUPD_VERBOSE") != NULL) {
		gint nvals = blkid_probe_numof_values(pr);
		for (gint i = 0; i < nvals; i++) {
			const gchar *name = NULL;
			if (blkid_probe_get_value(pr, i, &name, &data, &datalen) == 0)
				g_debug("%s=%s", name, data);
		}
	}

	/* extract block attributes */
	if (blkid_probe_lookup_value(pr, "TYPE", &data, &datalen) == 0)
		fu_block_partition_set_fs_type(self, data, datalen);
	if (blkid_probe_lookup_value(pr, "UUID", &data, &datalen) == 0)
		fu_block_partition_set_fs_uuid(self, data, datalen);
#else
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "not supported as <blkid.h> not found");
	return FALSE;
#endif

	/* save response */
	if (event != NULL) {
		if (priv->fs_type != NULL)
			fu_device_event_set_str(event, "FsType", priv->fs_type);
		if (priv->fs_uuid != NULL)
			fu_device_event_set_str(event, "FsUuid", priv->fs_uuid);
	}
	/* success */
	return TRUE;
}

static void
fu_block_partition_finalize(GObject *object)
{
	FuBlockPartition *self = FU_BLOCK_PARTITION(object);
	FuBlockPartitionPrivate *priv = GET_PRIVATE(self);

	g_free(priv->fs_type);
	g_free(priv->fs_uuid);

	G_OBJECT_CLASS(fu_block_partition_parent_class)->finalize(object);
}

static void
fu_block_partition_init(FuBlockPartition *self)
{
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_block_partition_class_init(FuBlockPartitionClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_block_partition_finalize;
	device_class->to_string = fu_block_partition_to_string;
	device_class->setup = fu_block_partition_setup;
	device_class->incorporate = fu_block_partition_incorporate;
}
