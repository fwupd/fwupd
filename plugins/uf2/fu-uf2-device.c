/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-uf2-device.h"
#include "fu-uf2-firmware.h"

struct _FuUf2Device {
	FuUdevDevice parent_instance;
	guint64 family_id;
	FuVolume *volume; /* non-null when fwupd has mounted it privately */
};

G_DEFINE_TYPE(FuUf2Device, fu_uf2_device, FU_TYPE_UDEV_DEVICE)

static FuFirmware *
fu_uf2_device_prepare_firmware(FuDevice *device,
			       GBytes *fw,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuUf2Device *self = FU_UF2_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_uf2_firmware_new();

	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;

	/* check the family_id matches if we can read the old firmware */
	if (self->family_id > 0 && fu_firmware_get_idx(firmware) > 0 &&
	    self->family_id != fu_firmware_get_idx(firmware)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "family ID was different, expected 0x%08x and got 0x%08x",
			    (guint)self->family_id,
			    (guint)fu_firmware_get_idx(firmware));
		return NULL;
	}

	/* success: but return the raw data */
	return fu_firmware_new_from_bytes(fw);
}

static gboolean
fu_uf2_device_probe_current_fw(FuDevice *device, GBytes *fw, GError **error)
{
	g_autofree gchar *csum_sha256 = NULL;
	g_autoptr(FuFirmware) firmware = fu_uf2_firmware_new();
	g_autoptr(GBytes) fw_raw = NULL;

	/* parse to get version */
	if (!fu_firmware_parse(firmware, fw, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;
	if (fu_firmware_get_version(firmware) != NULL)
		fu_device_set_version(device, fu_firmware_get_version(firmware));

	/* add instance ID for quirks */
	if (fu_firmware_get_idx(firmware) != 0x0) {
		fu_device_add_instance_u32(device,
					   "FAMILY",
					   (guint32)fu_firmware_get_idx(firmware));
	}
	fu_device_build_instance_id_quirk(device, NULL, "UF2", "FAMILY", NULL);

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
fu_block_device_get_full_path(FuUf2Device *self, const gchar *filename, GError **error)
{
	const gchar *devfile = fu_udev_device_get_device_file(FU_UDEV_DEVICE(self));
	g_autoptr(FuVolume) volume = NULL;
	g_autofree gchar *mount_point = NULL;

	/* sanity check */
	if (devfile == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "invalid path: no devfile");
		return NULL;
	}

	/* find volume */
	volume = fu_common_get_volume_by_device(devfile, error);
	if (volume == NULL)
		return NULL;

	/* success */
	mount_point = fu_volume_get_mount_point(volume);
	return g_build_filename(mount_point, filename, NULL);
}

static gboolean
fu_block_device_write_firmware(FuDevice *device,
			       FuFirmware *firmware,
			       FuProgress *progress,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuUf2Device *self = FU_UF2_DEVICE(device);
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
	fn = fu_block_device_get_full_path(self, "FIRMWARE.UF2", error);
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
	FuUf2Device *self = FU_UF2_DEVICE(device);
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GInputStream) istr = NULL;

	/* open for reading */
	fn = fu_block_device_get_full_path(self, "CURRENT.UF2", error);
	if (fn == NULL)
		return NULL;
	file = g_file_new_for_path(fn);
	istr = G_INPUT_STREAM(g_file_read(file, NULL, error));
	if (istr == NULL)
		return NULL;

	/* read all in one big chunk */
	return fu_common_get_contents_stream(istr, G_MAXUINT32, error);
}

static FuFirmware *
fu_uf2_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_uf2_firmware_new();
	g_autoptr(GBytes) fw = NULL;

	fw = fu_device_dump_firmware(device, progress, error);
	if (fw == NULL)
		return NULL;
	if (!fu_firmware_parse(firmware, fw, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;

	return g_steal_pointer(&firmware);
}

static gboolean
fu_uf2_device_volume_mount(FuUf2Device *self, GError **error)
{
	const gchar *devfile = fu_udev_device_get_device_file(FU_UDEV_DEVICE(self));

	/* mount volume if required */
	self->volume = fu_common_get_volume_by_device(devfile, error);
	if (self->volume == NULL)
		return FALSE;
	return fu_volume_mount(self->volume, error);
}

static gboolean
fu_uf2_device_check_volume_mounted_cb(FuDevice *self, gpointer user_data, GError **error)
{
	const gchar *devfile = fu_udev_device_get_device_file(FU_UDEV_DEVICE(user_data));
	g_autoptr(FuVolume) volume = NULL;

	/* mount volume if required */
	volume = fu_common_get_volume_by_device(devfile, error);
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

	/* wait for the user session to auto-mount the volume -- ideally we want to avoid using
	 * fu_volume_mount() which would make the volume only accessible by the fwupd user */
	if (!fu_device_retry_full(device,
				  fu_uf2_device_check_volume_mounted_cb,
				  20, /* count */
				  50, /* ms */
				  device,
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
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_autofree gchar *fn1 = NULL;
	g_autofree gchar *fn2 = NULL;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* this has to exist */
	fn1 = fu_block_device_get_full_path(self, "INFO_UF2.TXT", error);
	if (fn1 == NULL)
		return FALSE;
	if (!g_file_get_contents(fn1, &buf, &bufsz, error))
		return FALSE;
	lines = fu_common_strnsplit(buf, bufsz, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix(lines[i], "Model: ")) {
			fu_device_set_name(device, lines[i] + 7);
		} else if (g_str_has_prefix(lines[i], "Board-ID: ")) {
			fu_device_add_instance_strsafe(device, "BOARD", lines[i] + 10);
		}
	}
	fu_device_build_instance_id(device, NULL, "UF2", "BOARD", NULL);

	/* this might exist */
	fn2 = fu_block_device_get_full_path(self, "CURRENT.UF2", error);
	fw = fu_common_get_contents_bytes(fn2, NULL);
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
fu_uf2_device_probe(FuDevice *device, GError **error)
{
	GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(device));
	const gchar *tmp;
	guint64 vid = 0;
	guint64 pid = 0;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_uf2_device_parent_class)->probe(device, error))
		return FALSE;

	/* check is valid */
	tmp = g_udev_device_get_property(udev_device, "ID_BUS");
	if (g_strcmp0(tmp, "usb") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct ID_BUS=%s, expected usb",
			    tmp);
		return FALSE;
	}
	tmp = g_udev_device_get_property(udev_device, "ID_FS_TYPE");
	if (g_strcmp0(tmp, "vfat") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct ID_FS_TYPE=%s, expected vfat",
			    tmp);
		return FALSE;
	}

	/* set the physical ID */
	if (!fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "block", error))
		return FALSE;

	/* more instance IDs */
	tmp = g_udev_device_get_property(udev_device, "ID_VENDOR_ID");
	if (tmp != NULL)
		vid = g_ascii_strtoull(tmp, NULL, 16);
	if (vid != 0x0)
		fu_device_add_instance_u16(device, "VID", vid);
	tmp = g_udev_device_get_property(udev_device, "ID_MODEL_ID");
	if (tmp != NULL)
		pid = g_ascii_strtoull(tmp, NULL, 16);
	if (pid != 0x0)
		fu_device_add_instance_u16(device, "PID", pid);
	tmp = g_udev_device_get_property(udev_device, "ID_FS_UUID");
	fu_device_add_instance_str(device, "UUID", tmp);
	if (!fu_device_build_instance_id_quirk(device, error, "USB", "VID", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", "UUID", NULL))
		return FALSE;

	/* vendor-id */
	if (vid != 0x0) {
		g_autofree gchar *vendor_id = g_strdup_printf("USB:0x%04X", (guint)vid);
		fu_device_add_vendor_id(device, vendor_id);
	}

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
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2);	/* reload */
}

static void
fu_uf2_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUf2Device *self = FU_UF2_DEVICE(device);
	FU_DEVICE_CLASS(fu_uf2_device_parent_class)->to_string(device, idt, str);
	if (self->family_id > 0)
		fu_common_string_append_kx(str, idt, "FamilyId", self->family_id);
}

static void
fu_uf2_device_init(FuUf2Device *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.microsoft.uf2");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
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
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	GObjectClass *klass_object = G_OBJECT_CLASS(klass);
	klass_object->finalize = fu_uf2_device_finalize;
	klass_device->to_string = fu_uf2_device_to_string;
	klass_device->probe = fu_uf2_device_probe;
	klass_device->setup = fu_uf2_device_setup;
	klass_device->open = fu_uf2_device_open;
	klass_device->close = fu_uf2_device_close;
	klass_device->prepare_firmware = fu_uf2_device_prepare_firmware;
	klass_device->set_progress = fu_uf2_device_set_progress;
	klass_device->read_firmware = fu_uf2_device_read_firmware;
	klass_device->write_firmware = fu_block_device_write_firmware;
	klass_device->dump_firmware = fu_block_device_dump_firmware;
}
