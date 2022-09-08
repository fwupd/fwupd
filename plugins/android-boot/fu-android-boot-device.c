/*
 * Copyright (C) 2022 Dylan Van Assche <me@dylanvanassche.be>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <fcntl.h>
#include <string.h>

#include "fu-android-boot-device.h"

#define ANDROID_BOOT_UNKNOWN_VERSION "0.0.0"
#define ANDROID_BOOT_SECTOR_SIZE     512

struct _FuAndroidBootDevice {
	FuUdevDevice parent_instance;
	gchar *label;
	gchar *uuid;
	gchar *boot_slot;
	guint64 max_size;
};

G_DEFINE_TYPE(FuAndroidBootDevice, fu_android_boot_device, FU_TYPE_UDEV_DEVICE)

static void
fu_android_boot_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuAndroidBootDevice *self = FU_ANDROID_BOOT_DEVICE(device);

	fu_string_append(str, idt, "BootSlot", self->boot_slot);
	fu_string_append(str, idt, "Label", self->label);
	fu_string_append(str, idt, "UUID", self->uuid);
	fu_string_append_kx(str, idt, "MaxSize", self->max_size);
}

static gboolean
fu_android_boot_device_probe(FuDevice *device, GError **error)
{
	FuAndroidBootDevice *self = FU_ANDROID_BOOT_DEVICE(device);
	GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(device));
	guint64 sectors = 0;
	guint64 size = 0;
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

	/* extract label and check if it matches boot slot*/
	if (g_udev_device_has_property(udev_device, "ID_PART_ENTRY_NAME")) {
		self->label =
		    g_strdup(g_udev_device_get_property(udev_device, "ID_PART_ENTRY_NAME"));

		/* Use label as device name */
		fu_device_set_name(device, self->label);

		/* If the device has A/B partitioning, compare boot slot to only expose partitions
		 * in-use */
		if (self->boot_slot != NULL && !g_str_has_suffix(self->label, self->boot_slot)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "device is on a different bootslot");
			return FALSE;
		}
	}

	/* set max firmware size, required to avoid writing firmware bigger than partition */
	if (!g_udev_device_has_property(udev_device, "ID_PART_ENTRY_SIZE")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device does not expose its size");
		return FALSE;
	}

	sectors = g_udev_device_get_property_as_uint64(udev_device, "ID_PART_ENTRY_SIZE");
	size = sectors * ANDROID_BOOT_SECTOR_SIZE;
	self->max_size = size;

	/* extract partition UUID and require it for supporting a device */
	if (!g_udev_device_has_property(udev_device, "ID_PART_ENTRY_UUID")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device does not have a UUID");
		return FALSE;
	}
	self->uuid = g_strdup(g_udev_device_get_property(udev_device, "ID_PART_ENTRY_UUID"));

	/* extract serial number and set it */
	fu_device_set_serial(device, g_hash_table_lookup(cmdline, "androidboot.serialno"));

	/*
	 * Some devices don't have unique TYPE UUIDs, add the partition label to make them truly
	 * unique Devices have a fixed partition scheme anyway because they originally have Android
	 * which has such requirements.
	 */
	fu_device_add_instance_strsafe(device, "UUID", self->uuid);
	fu_device_add_instance_strsafe(device, "LABEL", self->label);
	fu_device_add_instance_strsafe(device, "SLOT", self->boot_slot);

	/* GUID based on UUID / UUID, label / UUID, label, slot */
	fu_device_build_instance_id(device, NULL, "DRIVE", "UUID", NULL);
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

	/* set the firmware maximum size based on partition size or from quirk */
	fu_device_set_firmware_size_max(device, self->max_size);

	return TRUE;
}

static gboolean
fu_android_boot_device_open(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* FuUdevDevice->open */
	if (!FU_DEVICE_CLASS(fu_android_boot_device_parent_class)->open(device, &error_local)) {
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED)) {
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
			     GPtrArray *chunks,
			     FuProgress *progress,
			     GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);

	/* rewind */
	if (!fu_udev_device_seek(FU_UDEV_DEVICE(self), 0x0, error)) {
		g_prefix_error(error, "failed to rewind: ");
		return FALSE;
	}

	/* write each chunk */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_udev_device_pwrite(FU_UDEV_DEVICE(self),
					   fu_chunk_get_address(chk),
					   fu_chunk_get_data(chk),
					   fu_chunk_get_data_sz(chk),
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
	g_autoptr(GPtrArray) chunks = NULL;
	gsize bufsz = fu_device_get_firmware_size_max(FU_DEVICE(self));
	g_autofree guint8 *buf = g_malloc0(bufsz);

	chunks = fu_chunk_array_new(buf, bufsz, 0x0, 0x0, 10 * 1024);

	if (g_getenv("FWUPD_ANDROID_BOOT_VERBOSE") != NULL)
		fu_dump_raw(G_LOG_DOMAIN, "erase", buf, bufsz);

	if (!fu_android_boot_device_write(self, chunks, progress, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_android_boot_device_verify(FuAndroidBootDevice *self,
			      GPtrArray *chunks,
			      FuProgress *progress,
			      GError **error)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);

	/* verify each chunk */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autofree guint8 *buf = g_malloc0(fu_chunk_get_data_sz(chk));
		g_autoptr(GBytes) blob1 = fu_chunk_get_bytes(chk);
		g_autoptr(GBytes) blob2 = NULL;

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
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* get data to write */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	if (g_getenv("FWUPD_ANDROID_BOOT_VERBOSE") != NULL)
		fu_dump_bytes(G_LOG_DOMAIN, "write", fw);

	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, 10 * 1024);

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

		if (!fu_strtoull(value, &size, 0, G_MAXUINT32, error))
			return FALSE;
		self->max_size = size;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
	return FALSE;
}

static void
fu_android_boot_device_finalize(GObject *obj)
{
	FuAndroidBootDevice *self = FU_ANDROID_BOOT_DEVICE(obj);
	G_OBJECT_CLASS(fu_android_boot_device_parent_class)->finalize(obj);
	g_free(self->boot_slot);
	g_free(self->label);
	g_free(self->uuid);
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
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
				 FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
				     FU_UDEV_DEVICE_FLAG_OPEN_SYNC);
	fu_device_add_icon(FU_DEVICE(self), "computer");

	/*
	 * Fallback for ABL without version reporting, fwupd will always provide an upgrade in this
	 * case. Once upgraded, the version reporting will be available and the update notification
	 * will disappear. If version reporting is available, the reported version is set.
	 */
	fu_device_set_version(FU_DEVICE(self), ANDROID_BOOT_UNKNOWN_VERSION);
}

static void
fu_android_boot_device_class_init(FuAndroidBootDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	GObjectClass *klass_object = G_OBJECT_CLASS(klass);

	klass_object->finalize = fu_android_boot_device_finalize;
	klass_device->probe = fu_android_boot_device_probe;
	klass_device->open = fu_android_boot_device_open;
	klass_device->write_firmware = fu_android_boot_device_write_firmware;
	klass_device->to_string = fu_android_boot_device_to_string;
	klass_device->set_quirk_kv = fu_android_boot_device_set_quirk_kv;
}
