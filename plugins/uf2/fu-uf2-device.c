/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uf2-device.h"
#include "fu-uf2-firmware.h"

struct _FuUf2Device {
	FuBlockPartition parent_instance;
	guint64 family_id;
	FuVolume *volume; /* non-null when fwupd has mounted it privately */
};

G_DEFINE_TYPE(FuUf2Device, fu_uf2_device, FU_TYPE_BLOCK_PARTITION)

static FuFirmware *
fu_uf2_device_prepare_firmware(FuDevice *device,
			       GInputStream *stream,
			       FuProgress *progress,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuUf2Device *self = FU_UF2_DEVICE(device);
	g_autoptr(FuFirmware) firmware_raw = fu_firmware_new();
	g_autoptr(FuFirmware) firmware = fu_uf2_firmware_new();

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* check the family_id matches if we can read the old firmware */
	if (self->family_id > 0 && fu_firmware_get_idx(firmware) > 0 &&
	    self->family_id != fu_firmware_get_idx(firmware)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "family ID was different, expected 0x%08x and got 0x%08x",
			    (guint)self->family_id,
			    (guint)fu_firmware_get_idx(firmware));
		return NULL;
	}

	/* success: but return the raw data */
	if (!fu_firmware_parse_stream(firmware_raw, stream, 0x0, flags, error))
		return NULL;
	return g_steal_pointer(&firmware_raw);
}

static gboolean
fu_uf2_device_probe_current_fw(FuDevice *device, GBytes *fw, GError **error)
{
	g_autofree gchar *csum_sha256 = NULL;
	g_autoptr(FuFirmware) firmware = fu_uf2_firmware_new();
	g_autoptr(GBytes) fw_raw = NULL;

	/* parse to get version */
	if (!fu_firmware_parse_bytes(firmware, fw, 0x0, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;
	if (fu_firmware_get_version(firmware) != NULL)
		fu_device_set_version(device, fu_firmware_get_version(firmware));

	/* add instance ID for quirks */
	if (fu_firmware_get_idx(firmware) != 0x0) {
		fu_device_add_instance_u32(device,
					   "FAMILY",
					   (guint32)fu_firmware_get_idx(firmware));
	}
	(void)fu_device_build_instance_id_full(device,
					       FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					       NULL,
					       "UF2",
					       "FAMILY",
					       NULL);

	/* add device checksum */
	fw_raw = fu_firmware_get_bytes(firmware, error);
	if (fw_raw == NULL)
		return FALSE;
	csum_sha256 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, fw_raw);
	fu_device_add_checksum(device, csum_sha256);

	/* success */
	return TRUE;
}

static gchar *
fu_uf2_device_get_full_path(FuUf2Device *self, const gchar *filename, GError **error)
{
	const gchar *devfile = fu_udev_device_get_device_file(FU_UDEV_DEVICE(self));
	g_autoptr(FuVolume) volume = NULL;
	g_autofree gchar *mount_point = NULL;

	/* sanity check */
	if (devfile == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "invalid path: no devfile");
		return NULL;
	}

	/* find volume */
	volume = fu_volume_new_by_device(devfile, error);
	if (volume == NULL)
		return NULL;

	/* success */
	mount_point = fu_volume_get_mount_point(volume);
	return g_build_filename(mount_point, filename, NULL);
}

static gboolean
fu_uf2_device_write_firmware(FuDevice *device,
			     FuFirmware *firmware,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuUf2Device *self = FU_UF2_DEVICE(device);
	g_autofree gchar *fn = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* get blob */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* open file for writing; no cleverness */
	fn = fu_uf2_device_get_full_path(self, "FIRMWARE.UF2", error);
	if (fn == NULL)
		return FALSE;

	/* success */
	return fu_device_set_contents(device, fn, stream, progress, error);
}

static GBytes *
fu_uf2_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuUf2Device *self = FU_UF2_DEVICE(device);
	g_autofree gchar *fn = NULL;

	/* open for reading */
	fn = fu_uf2_device_get_full_path(self, "CURRENT.UF2", error);
	if (fn == NULL)
		return NULL;
	return fu_device_get_contents_bytes(device, fn, progress, error);
}

static FuFirmware *
fu_uf2_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_uf2_firmware_new();
	g_autoptr(GBytes) fw = NULL;

	fw = fu_device_dump_firmware(device, progress, error);
	if (fw == NULL)
		return NULL;
	if (!fu_firmware_parse_bytes(firmware, fw, 0x0, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;

	return g_steal_pointer(&firmware);
}

static gboolean
fu_uf2_device_volume_mount(FuUf2Device *self, GError **error)
{
	const gchar *devfile = fu_udev_device_get_device_file(FU_UDEV_DEVICE(self));

	/* mount volume if required */
	self->volume = fu_volume_new_by_device(devfile, error);
	if (self->volume == NULL)
		return FALSE;
	return fu_volume_mount(self->volume, error);
}

static gboolean
fu_uf2_device_check_volume_mounted_cb(FuDevice *device, gpointer user_data, GError **error)
{
	const gchar *devfile = fu_udev_device_get_device_file(FU_UDEV_DEVICE(device));
	g_autoptr(FuVolume) volume = NULL;

	if (devfile == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "invalid path: no devfile");
		return FALSE;
	}

	/* mount volume if required */
	volume = fu_volume_new_by_device(devfile, error);
	if (volume == NULL)
		return FALSE;
	if (!fu_volume_is_mounted(volume)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "is not mounted");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uf2_device_open(FuDevice *device, GError **error)
{
	FuUf2Device *self = FU_UF2_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	/* FuUdevDevice->open() */
	if (!FU_DEVICE_CLASS(fu_uf2_device_parent_class)->open(device, error))
		return FALSE;

	/* wait for the user session to auto-mount the volume -- ideally we want to avoid using
	 * fu_volume_mount() which would make the volume only accessible by the fwupd user */
	if (!fu_device_retry_full(device,
				  fu_uf2_device_check_volume_mounted_cb,
				  20, /* count */
				  50, /* ms */
				  NULL,
				  &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			/* maybe no session running? */
			if (!fu_uf2_device_volume_mount(self, error))
				return FALSE;
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uf2_device_close(FuDevice *device, GError **error)
{
	FuUf2Device *self = FU_UF2_DEVICE(device);

	/* FuUdevDevice->close() */
	if (!FU_DEVICE_CLASS(fu_uf2_device_parent_class)->close(device, error))
		return FALSE;

	/* we only do this when mounting for the fwupd user */
	if (self->volume != NULL) {
		if (!fu_volume_unmount(self->volume, error))
			return FALSE;
		g_clear_object(&self->volume);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uf2_device_setup(FuDevice *device, GError **error)
{
	FuUf2Device *self = FU_UF2_DEVICE(device);
	g_autofree gchar *fn1 = NULL;
	g_autofree gchar *fn2 = NULL;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GBytes) blob_txt = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* FuUdevDevice->setup() */
	if (!FU_DEVICE_CLASS(fu_uf2_device_parent_class)->setup(device, error))
		return FALSE;

	/* sanity check filesystem type */
	if (g_strcmp0(fu_block_partition_get_fs_type(FU_BLOCK_PARTITION(self)), "vfat") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "filesystem type of %s unsupported, expected vfat",
			    fu_block_partition_get_fs_type(FU_BLOCK_PARTITION(self)));
		return FALSE;
	}

	/* only add UUID if it is set */
	if (fu_block_partition_get_fs_uuid(FU_BLOCK_PARTITION(self)) != NULL) {
		fu_device_add_instance_str(
		    device,
		    "UUID",
		    fu_block_partition_get_fs_uuid(FU_BLOCK_PARTITION(self)));
		if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", "UUID", NULL))
			return FALSE;
	}

	/* this has to exist */
	fn1 = fu_uf2_device_get_full_path(self, "INFO_UF2.TXT", error);
	if (fn1 == NULL)
		return FALSE;
	blob_txt = fu_device_get_contents_bytes(device, fn1, NULL, error);
	lines = fu_strsplit(g_bytes_get_data(blob_txt, NULL), g_bytes_get_size(blob_txt), "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix(lines[i], "Model: ")) {
			fu_device_set_name(device, lines[i] + 7);
		} else if (g_str_has_prefix(lines[i], "Board-ID: ")) {
			fu_device_add_instance_strsafe(device, "BOARD", lines[i] + 10);
		}
	}
	fu_device_build_instance_id(device, NULL, "UF2", "BOARD", NULL);

	/* this might exist */
	fn2 = fu_uf2_device_get_full_path(self, "CURRENT.UF2", error);
	if (fn2 == NULL)
		return FALSE;
	fw = fu_device_get_contents_bytes(device, fn2, NULL, NULL);
	if (fw != NULL) {
		if (!fu_uf2_device_probe_current_fw(device, fw, error))
			return FALSE;
	} else {
		fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uf2_device_usb_probe(FuUf2Device *self, FuDevice *usb_device, GError **error)
{
	/* copy the VID and PID */
	if (!fu_device_probe(usb_device, error))
		return FALSE;
	fu_device_incorporate(FU_DEVICE(self),
			      usb_device,
			      FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
				  FU_DEVICE_INCORPORATE_FLAG_VID | FU_DEVICE_INCORPORATE_FLAG_PID);
	if (!fu_device_build_instance_id_full(FU_DEVICE(self),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "USB",
					      "VID",
					      NULL))
		return FALSE;
	if (!fu_device_build_instance_id(FU_DEVICE(self), error, "USB", "VID", "PID", NULL))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_uf2_device_probe(FuDevice *device, GError **error)
{
	FuUf2Device *self = FU_UF2_DEVICE(device);
	g_autoptr(FuDevice) usb_device = NULL;

	/* get USB properties */
	usb_device = fu_device_get_backend_parent_with_subsystem(device, "usb:usb_device", error);
	if (usb_device == NULL)
		return FALSE;
	if (!fu_uf2_device_usb_probe(self, usb_device, error))
		return FALSE;

	/* check the quirk matched to avoid mounting *all* vfat devices */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not marked as updatable in uf2.quirk");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_uf2_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_uf2_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUf2Device *self = FU_UF2_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "FamilyId", self->family_id);
}

static void
fu_uf2_device_vid_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	fu_device_add_instance_u16(device, "VID", fu_device_get_vid(device));
}

static void
fu_uf2_device_pid_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	fu_device_add_instance_u16(device, "PID", fu_device_get_pid(device));
}

static void
fu_uf2_device_init(FuUf2Device *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.microsoft.uf2");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	g_signal_connect(FU_DEVICE(self),
			 "notify::vid",
			 G_CALLBACK(fu_uf2_device_vid_notify_cb),
			 NULL);
	g_signal_connect(FU_DEVICE(self),
			 "notify::pid",
			 G_CALLBACK(fu_uf2_device_pid_notify_cb),
			 NULL);
}

static void
fu_uf2_device_finalize(GObject *obj)
{
	FuUf2Device *self = FU_UF2_DEVICE(obj);

	/* should be done by ->close(), but check to be sure */
	if (self->volume != NULL)
		g_object_unref(self->volume);

	G_OBJECT_CLASS(fu_uf2_device_parent_class)->finalize(obj);
}

static void
fu_uf2_device_class_init(FuUf2DeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_uf2_device_finalize;
	device_class->to_string = fu_uf2_device_to_string;
	device_class->probe = fu_uf2_device_probe;
	device_class->setup = fu_uf2_device_setup;
	device_class->open = fu_uf2_device_open;
	device_class->close = fu_uf2_device_close;
	device_class->prepare_firmware = fu_uf2_device_prepare_firmware;
	device_class->set_progress = fu_uf2_device_set_progress;
	device_class->read_firmware = fu_uf2_device_read_firmware;
	device_class->write_firmware = fu_uf2_device_write_firmware;
	device_class->dump_firmware = fu_uf2_device_dump_firmware;
}
