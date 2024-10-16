/*
 * Copyright 2022 Dylan Van Assche <me@dylanvanassche.be>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fcntl.h>
#include <string.h>

#include "fu-android-boot-device.h"

#define ANDROID_BOOT_UNKNOWN_VERSION "0.0.0"
#define ANDROID_BOOT_SECTOR_SIZE     512

struct _FuAndroidBootDevice {
	FuBlockPartition parent_instance;
	gchar *boot_slot;
	guint64 max_size;
};

G_DEFINE_TYPE(FuAndroidBootDevice, fu_android_boot_device, FU_TYPE_BLOCK_PARTITION)

static void
fu_android_boot_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuAndroidBootDevice *self = FU_ANDROID_BOOT_DEVICE(device);

	fwupd_codec_string_append(str, idt, "BootSlot", self->boot_slot);
	fwupd_codec_string_append_hex(str, idt, "MaxSize", self->max_size);
}

static gboolean
fu_android_boot_device_probe(FuDevice *device, GError **error)
{
	FuAndroidBootDevice *self = FU_ANDROID_BOOT_DEVICE(device);
	guint64 sectors = 0;
	guint64 size = 0;
	g_autofree gchar *prop_size = NULL;
	g_autoptr(GHashTable) cmdline = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_android_boot_device_parent_class)->probe(device, error))
		return FALSE;

	/* get kernel cmdline */
	cmdline = fu_kernel_get_cmdline(error);
	if (cmdline == NULL)
		return FALSE;

	/* set the physical ID */
	if (!fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "block", error))
		return FALSE;

	/* extract boot slot if available */
	self->boot_slot = g_strdup(g_hash_table_lookup(cmdline, "androidboot.slot_suffix"));

	/* set max firmware size, required to avoid writing firmware bigger than partition */
	prop_size = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					      "size",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      NULL);
	if (prop_size == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device does not expose its size");
		return FALSE;
	}
	if (!fu_strtoull(prop_size, &sectors, 0x0, G_MAXUINT64, FU_INTEGER_BASE_AUTO, error))
		return FALSE;
	size = sectors * ANDROID_BOOT_SECTOR_SIZE;
	self->max_size = size;

	/* extract serial number and set it */
	fu_device_set_serial(device, g_hash_table_lookup(cmdline, "androidboot.serialno"));

	/* set the firmware maximum size based on partition size or from quirk */
	fu_device_set_firmware_size_max(device, self->max_size);

	return TRUE;
}

static gboolean
fu_android_boot_device_setup(FuDevice *device, GError **error)
{
	FuAndroidBootDevice *self = FU_ANDROID_BOOT_DEVICE(device);
	const gchar *fs_label;

	/* FuBlockPartition->setup() */
	if (!FU_DEVICE_CLASS(fu_android_boot_device_parent_class)->setup(device, error))
		return FALSE;

	/* extract label and check if it matches boot slot */
	fs_label = fu_block_partition_get_fs_label(FU_BLOCK_PARTITION(self));
	if (fs_label != NULL) {
		fu_device_set_name(device, fs_label);

		/* If the device has A/B partitioning, compare boot slot to only expose partitions
		 * in-use */
		if (self->boot_slot != NULL && !g_str_has_suffix(fs_label, self->boot_slot)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "device is on a different bootslot");
			return FALSE;
		}
	}

	/*
	 * Some devices don't have unique TYPE UUIDs, add the partition label to make them truly
	 * unique Devices have a fixed partition scheme anyway because they originally have Android
	 * which has such requirements.
	 */
	fu_device_add_instance_strsafe(device,
				       "UUID",
				       fu_block_partition_get_fs_uuid(FU_BLOCK_PARTITION(self)));
	fu_device_add_instance_strsafe(device, "LABEL", fs_label);
	fu_device_add_instance_strsafe(device, "SLOT", self->boot_slot);

	/* GUID based on UUID / UUID, label / UUID, label, slot */
	if (!fu_device_build_instance_id(device, error, "DRIVE", "UUID", NULL))
		return FALSE;
	fu_device_build_instance_id(device, NULL, "DRIVE", "UUID", "LABEL", NULL);
	fu_device_build_instance_id(device, NULL, "DRIVE", "UUID", "LABEL", "SLOT", NULL);

	/* quirks will have matched now */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device is not updatable");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_android_boot_device_open(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* FuUdevDevice->open */
	if (!FU_DEVICE_CLASS(fu_android_boot_device_parent_class)->open(device, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_PERMISSION_DENIED)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    error_local->message);
			return FALSE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_android_boot_device_write(FuAndroidBootDevice *self,
			     FuChunkArray *chunks,
			     FuProgress *progress,
			     GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	/* rewind */
	if (!fu_udev_device_seek(FU_UDEV_DEVICE(self), 0x0, error)) {
		g_prefix_error(error, "failed to rewind: ");
		return FALSE;
	}

	/* write each chunk */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GBytes) blob = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		blob = fu_chunk_get_bytes(chk, error);
		if (blob == NULL)
			return FALSE;
		if (!fu_udev_device_pwrite(FU_UDEV_DEVICE(self),
					   fu_chunk_get_address(chk),
					   g_bytes_get_data(blob, NULL),
					   g_bytes_get_size(blob),
					   error)) {
			g_prefix_error(error,
				       "failed to write @0x%x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_android_boot_device_erase(FuAndroidBootDevice *self, FuProgress *progress, GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;
	gsize bufsz = fu_device_get_firmware_size_max(FU_DEVICE(self));
	g_autofree guint8 *buf = g_malloc0(bufsz);
	g_autoptr(GBytes) fw = g_bytes_new_take(g_steal_pointer(&buf), bufsz);

	chunks = fu_chunk_array_new_from_bytes(fw,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       10 * 1024);
	return fu_android_boot_device_write(self, chunks, progress, error);
}

static gboolean
fu_android_boot_device_verify(FuAndroidBootDevice *self,
			      FuChunkArray *chunks,
			      FuProgress *progress,
			      GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	/* verify each chunk */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autofree guint8 *buf = NULL;
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GBytes) blob1 = NULL;
		g_autoptr(GBytes) blob2 = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		buf = g_malloc0(fu_chunk_get_data_sz(chk));
		if (!fu_udev_device_pread(FU_UDEV_DEVICE(self),
					  fu_chunk_get_address(chk),
					  buf,
					  fu_chunk_get_data_sz(chk),
					  error)) {
			g_prefix_error(error,
				       "failed to read @0x%x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		blob1 = fu_chunk_get_bytes(chk, error);
		if (blob1 == NULL)
			return FALSE;
		blob2 = g_bytes_new_static(buf, fu_chunk_get_data_sz(chk));
		if (!fu_bytes_compare(blob1, blob2, error)) {
			g_prefix_error(error,
				       "failed to verify @0x%x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	return TRUE;
}

static gboolean
fu_android_boot_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuAndroidBootDevice *self = FU_ANDROID_BOOT_DEVICE(device);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* get data to write */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						10 * 1024,
						error);
	if (chunks == NULL)
		return FALSE;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 72, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 7, NULL);

	/* erase, write, verify */
	if (!fu_android_boot_device_erase(self, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_android_boot_device_write(self, chunks, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_android_boot_device_verify(self, chunks, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	return TRUE;
}

static gboolean
fu_android_boot_device_set_quirk_kv(FuDevice *device,
				    const gchar *key,
				    const gchar *value,
				    GError **error)
{
	FuAndroidBootDevice *self = FU_ANDROID_BOOT_DEVICE(device);

	/* load from quirks */
	if (g_strcmp0(key, "AndroidBootVersionProperty") == 0) {
		g_autoptr(GHashTable) cmdline = NULL;
		const gchar *version = NULL;

		cmdline = fu_kernel_get_cmdline(error);
		if (cmdline == NULL)
			return FALSE;

		version = g_hash_table_lookup(cmdline, value);
		if (version != NULL)
			fu_device_set_version(device, version);
		return TRUE;
	}

	if (g_strcmp0(key, "AndroidBootPartitionMaxSize") == 0) {
		guint64 size = 0;

		if (!fu_strtoull(value, &size, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->max_size = size;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_android_boot_device_finalize(GObject *obj)
{
	FuAndroidBootDevice *self = FU_ANDROID_BOOT_DEVICE(obj);
	G_OBJECT_CLASS(fu_android_boot_device_parent_class)->finalize(obj);
	g_free(self->boot_slot);
}

static void
fu_android_boot_device_init(FuAndroidBootDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self), "Android Bootloader");
	fu_device_add_protocol(FU_DEVICE(self), "com.google.android_boot");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_SYNC);
	fu_device_add_icon(FU_DEVICE(self), "computer");

	/*
	 * Fallback for ABL without version reporting, fwupd will always provide an upgrade in this
	 * case. Once upgraded, the version reporting will be available and the update notification
	 * will disappear. If version reporting is available, the reported version is set.
	 */
	fu_device_set_version(FU_DEVICE(self), ANDROID_BOOT_UNKNOWN_VERSION);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
}

static void
fu_android_boot_device_class_init(FuAndroidBootDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_android_boot_device_finalize;
	device_class->probe = fu_android_boot_device_probe;
	device_class->setup = fu_android_boot_device_setup;
	device_class->open = fu_android_boot_device_open;
	device_class->write_firmware = fu_android_boot_device_write_firmware;
	device_class->to_string = fu_android_boot_device_to_string;
	device_class->set_quirk_kv = fu_android_boot_device_set_quirk_kv;
}
