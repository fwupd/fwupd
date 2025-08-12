/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#ifdef HAVE_MTD_USER_H
#include <mtd/mtd-user.h>
#endif

#ifdef HAVE_IOCTL_H
#include <sys/ioctl.h>
#endif

#include "fu-mtd-device.h"
#include "fu-mtd-ifd-device.h"

struct _FuMtdDevice {
	FuUdevDevice parent_instance;
	guint64 erasesize;
	guint64 metadata_offset;
	guint64 metadata_size;
	gboolean is_pci_device;
};

G_DEFINE_TYPE(FuMtdDevice, fu_mtd_device, FU_TYPE_UDEV_DEVICE)

#define FU_MTD_DEVICE_IOCTL_TIMEOUT 5000 /* ms */

static void
fu_mtd_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuMtdDevice *self = FU_MTD_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "EraseSize", self->erasesize);
	fwupd_codec_string_append_hex(str, idt, "MetadataOffset", self->metadata_offset);
	fwupd_codec_string_append_hex(str, idt, "MetadataSize", self->metadata_size);
	fwupd_codec_string_append_hex(str, idt, "IsPciDevice", self->is_pci_device);
}

static gchar *
fu_mtd_device_convert_version(FuDevice *device, guint64 version_raw)
{
	FuMtdDevice *self = FU_MTD_DEVICE(device);

	/* let's assume for now that any PCI device with pair version format uses B&R encoding */
	if (self->is_pci_device &&
	    fu_device_get_version_format(self) == FWUPD_VERSION_FORMAT_PAIR) {
		guint64 major = version_raw / 100;
		guint64 minor = version_raw % 100;

		return g_strdup_printf("%" G_GUINT64_FORMAT ".%02" G_GUINT64_FORMAT, major, minor);
	}

	return NULL;
}

static FuFirmware *
fu_mtd_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMtdDevice *self = FU_MTD_DEVICE(device);
	FuDeviceEvent *event = NULL;
	GType firmware_gtype = fu_device_get_firmware_gtype(device);
	const gchar *fn;
	g_autofree gchar *event_id = NULL;
	g_autoptr(FuFirmware) firmware = g_object_new(firmware_gtype, NULL);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GInputStream) stream_partial = NULL;

	/* need event ID */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED) ||
	    fu_context_has_flag(fu_device_get_context(FU_DEVICE(self)),
				FU_CONTEXT_FLAG_SAVE_EVENTS)) {
		event_id = g_strdup("MtdReadFirmware");
	}

	/* emulated */
	if (fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED)) {
		g_autoptr(GBytes) blob = NULL;
		event = fu_device_load_event(FU_DEVICE(self), event_id, error);
		if (event == NULL)
			return NULL;
		blob = fu_device_event_get_bytes(event, "Data", error);
		if (blob == NULL)
			return NULL;
		if (!fu_firmware_parse_bytes(firmware,
					     blob,
					     0x0,
					     FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
					     error)) {
			return NULL;
		}
		return g_steal_pointer(&firmware);
	}

	/* save */
	if (event_id != NULL)
		event = fu_device_save_event(FU_DEVICE(self), event_id);

	/* read contents at the search offset */
	fn = fu_udev_device_get_device_file(FU_UDEV_DEVICE(self));
	if (fn == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Not supported as no device file");
		return NULL;
	}
	stream = fu_input_stream_from_path(fn, error);
	if (stream == NULL) {
		g_prefix_error_literal(error, "failed to open device: ");
		return NULL;
	}
	if (self->metadata_size > 0) {
		stream_partial = fu_partial_input_stream_new(stream,
							     self->metadata_offset,
							     self->metadata_size,
							     error);
		if (stream_partial == NULL)
			return NULL;
	} else {
		stream_partial = g_object_ref(stream);
	}

	/* save response */
	if (event != NULL) {
		g_autoptr(GBytes) blob = NULL;
		blob = fu_input_stream_read_bytes(stream_partial, 0x0, G_MAXSIZE, progress, error);
		if (blob == NULL)
			return NULL;
		fu_device_event_set_bytes(event, "Data", blob);
	}

	/* parse as firmware image */
	if (!fu_firmware_parse_stream(firmware,
				      stream_partial,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
				      error)) {
		g_prefix_error_literal(error, "failed to parse image: ");
		return NULL;
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_mtd_device_metadata_load(FuMtdDevice *self, GError **error)
{
	GPtrArray *instance_ids;
	g_autoptr(FuFirmware) firmware_child = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GPtrArray) imgs = NULL;
	g_autoptr(FuFirmware) firmware = NULL;

	/* read firmware from stream */
	firmware = fu_mtd_device_read_firmware(FU_DEVICE(self), NULL, error);
	if (firmware == NULL)
		return FALSE;

	/* add each IFD image as a sub-device */
	imgs = fu_firmware_get_images(firmware);
	if (FU_IS_IFD_FIRMWARE(firmware)) {
		for (guint i = 0; i < imgs->len; i++) {
			FuIfdImage *img = g_ptr_array_index(imgs, i);
			g_autoptr(FuMtdIfdDevice) child =
			    fu_mtd_ifd_device_new(FU_DEVICE(self), img);
			fu_device_add_child(FU_DEVICE(self), FU_DEVICE(child));
		}
		return TRUE;
	}

	/* find the firmware child that matches any of the device GUID, then use the first
	 * child that have a version, and finally use the main firmware as a fallback */
	instance_ids = fu_device_get_instance_ids(FU_DEVICE(self));
	for (guint i = 0; i < instance_ids->len; i++) {
		const gchar *instance_id = g_ptr_array_index(instance_ids, i);
		g_autofree gchar *guid = fwupd_guid_hash_string(instance_id);
		firmware_child = fu_firmware_get_image_by_id(firmware, guid, NULL);
		if (firmware_child != NULL)
			break;
	}
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *firmare_tmp = g_ptr_array_index(imgs, i);
		if (fu_firmware_get_version(firmare_tmp) != NULL ||
		    fu_firmware_get_version_raw(firmare_tmp) != 0) {
			firmware_child = g_object_ref(firmare_tmp);
			break;
		}
	}
	if (firmware_child == NULL)
		firmware_child = g_object_ref(firmware);

	/* copy over the version */
	if (fu_firmware_get_version(firmware_child) != NULL) {
		fu_device_set_version(FU_DEVICE(self), /* nocheck:set-version */
				      fu_firmware_get_version(firmware_child));
	}
	if (fu_firmware_get_version_raw(firmware_child) != G_MAXUINT64) {
		fu_device_set_version_raw(FU_DEVICE(self),
					  fu_firmware_get_version_raw(firmware_child));
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mtd_device_setup(FuDevice *device, GError **error)
{
	FuMtdDevice *self = FU_MTD_DEVICE(device);
	GType firmware_gtype = fu_device_get_firmware_gtype(device);
	gsize firmware_size_max = fu_device_get_firmware_size_max(device);
	g_autoptr(GError) error_local = NULL;

	/* sanity check */
	if (self->metadata_offset > firmware_size_max) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "offset of metadata (0x%x) greater than image size (0x%x)",
			    (guint)self->metadata_offset,
			    (guint)firmware_size_max);
		return FALSE;
	}
	if (self->metadata_size > firmware_size_max - self->metadata_offset) {
		self->metadata_size = firmware_size_max - self->metadata_offset;
		g_debug("truncating metadata size to 0x%x", (guint)self->metadata_size);
	}

	/* nothing to do */
	if (firmware_gtype == G_TYPE_INVALID)
		return TRUE;
	if (!fu_mtd_device_metadata_load(self, &error_local)) {
		g_warning("no version metadata found: %s", error_local->message);
		return TRUE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mtd_device_open(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* FuUdevDevice->open */
	if (!FU_DEVICE_CLASS(fu_mtd_device_parent_class)->open(device, &error_local)) {
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

	/* success */
	return TRUE;
}

static gboolean
fu_mtd_device_probe(FuDevice *device, GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	FuMtdDevice *self = FU_MTD_DEVICE(device);
	guint64 flags = 0;
	guint64 size = 0;
	g_autofree gchar *attr_flags = NULL;
	g_autofree gchar *attr_size = NULL;
	g_autofree gchar *attr_name = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuDevice) parent_device = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_mtd_device_parent_class)->probe(device, error))
		return FALSE;

	/* set physical ID */
	if (!fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "mtd", error))
		return FALSE;

	/* flags have to exist */
	attr_flags = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
					       "flags",
					       FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					       &error_local);
	if (attr_flags == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "no MTD flags");
			return FALSE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	if (!fu_strtoull(attr_flags, &flags, 0, G_MAXUINT64, FU_INTEGER_BASE_AUTO, error))
		return FALSE;

	/* get name */
	attr_name = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					      "name",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      NULL);
	if (attr_name != NULL)
		fu_device_set_name(FU_DEVICE(self), attr_name);

	/* MTD devices backed by PCI should use that for identification */
	parent_device = fu_device_get_backend_parent_with_subsystem(device, "pci", NULL);
	if (parent_device != NULL) {
		self->is_pci_device = TRUE;

		fu_device_incorporate(
		    device,
		    parent_device,
		    FU_DEVICE_INCORPORATE_FLAG_VENDOR | FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
			FU_DEVICE_INCORPORATE_FLAG_VID | FU_DEVICE_INCORPORATE_FLAG_PID |
			FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);

		if (fu_device_get_version(device) == NULL)
			fu_device_set_version_raw(
			    device,
			    fu_pci_device_get_revision(FU_PCI_DEVICE(parent_device)));

		fu_device_add_instance_strsafe(device, "NAME", attr_name);
		fu_device_build_instance_id(device, NULL, "MTD", "NAME", NULL);
		fu_device_build_instance_id(device, NULL, "MTD", "VEN", "DEV", NULL);
		fu_device_build_instance_id(device, NULL, "MTD", "VEN", "DEV", "NAME", NULL);
	} else {
		const gchar *vendor;

		/* set vendor ID as the BIOS vendor */
		vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_MANUFACTURER);
		fu_device_build_vendor_id(device, "DMI", vendor);

		/* use vendor and product as an optional instance ID prefix */
		fu_device_add_instance_strsafe(device, "NAME", attr_name);
		fu_device_add_instance_strsafe(device, "VENDOR", vendor);
		fu_device_add_instance_strsafe(
		    device,
		    "PRODUCT",
		    fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_NAME));
		fu_device_build_instance_id(device, NULL, "MTD", "NAME", NULL);
		fu_device_build_instance_id(device, NULL, "MTD", "VENDOR", "NAME", NULL);
		fu_device_build_instance_id(device, NULL, "MTD", "VENDOR", "PRODUCT", "NAME", NULL);
	}

	/* get properties about the device */
	attr_size = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
					      "size",
					      FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					      error);
	if (attr_size == NULL)
		return FALSE;
	if (!fu_strtoull(attr_size, &size, 0, G_MAXUINT64, FU_INTEGER_BASE_AUTO, error))
		return FALSE;
	fu_device_set_firmware_size_max(device, size);
#ifdef HAVE_MTD_USER_H
	if ((flags & MTD_NO_ERASE) == 0) {
		g_autofree gchar *attr_erasesize = NULL;
		attr_erasesize = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
							   "erasesize",
							   FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
							   error);
		if (attr_erasesize == NULL)
			return FALSE;
		if (!fu_strtoull(attr_erasesize,
				 &self->erasesize,
				 0,
				 G_MAXUINT64,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
	}
	if (flags & MTD_WRITEABLE) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	}
#endif

	/* success */
	return TRUE;
}

static gboolean
fu_mtd_device_erase(FuMtdDevice *self, GInputStream *stream, FuProgress *progress, GError **error)
{
#ifdef HAVE_MTD_USER_H
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						self->erasesize,
						error);
	if (chunks == NULL)
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	/* erase each chunk */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		struct erase_info_user erase = {0x0};
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(FuIoctl) ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(self));

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		erase.start = fu_chunk_get_address(chk);
		erase.length = fu_chunk_get_data_sz(chk);

		/* the last chunk may be smaller than the erasesize. if it is, extend the last erase
		 * up to the erasesize */
		if (erase.length < self->erasesize) {
			g_debug("extending last erase from %" G_GUINT32_FORMAT
				" bytes to %" G_GUINT64_FORMAT " bytes",
				erase.length,
				self->erasesize);
			erase.length = self->erasesize;
		}

		if (!fu_ioctl_execute(ioctl,
				      MEMERASE,
				      (guint8 *)&erase,
				      sizeof(erase),
				      NULL,
				      FU_MTD_DEVICE_IOCTL_TIMEOUT,
				      FU_IOCTL_FLAG_NONE,
				      error)) {
			g_prefix_error(error, "failed to erase @0x%x: ", (guint)erase.start);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Not supported as mtd-user.h is unavailable");
	return FALSE;
#endif
}

static gboolean
fu_mtd_device_write(FuMtdDevice *self, FuChunkArray *chunks, FuProgress *progress, GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));

	/* rewind */
	if (!fu_udev_device_seek(FU_UDEV_DEVICE(self), 0x0, error)) {
		g_prefix_error_literal(error, "failed to rewind: ");
		return FALSE;
	}

	/* write each chunk */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
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

	/* success */
	return TRUE;
}

static gboolean
fu_mtd_device_verify(FuMtdDevice *self, FuChunkArray *chunks, FuProgress *progress, GError **error)
{
	/* progress */
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
		blob1 = fu_chunk_get_bytes(chk);
		blob2 = g_bytes_new_static(buf, fu_chunk_get_data_sz(chk));
		if (!fu_bytes_compare(blob1, blob2, error)) {
			g_prefix_error(error,
				       "failed to verify @0x%x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_mtd_device_write_verify(FuMtdDevice *self,
			   GInputStream *stream,
			   FuProgress *progress,
			   GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						10 * 1024,
						error);
	if (chunks == NULL)
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 50, NULL);

	/* write */
	if (!fu_mtd_device_write(self, chunks, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify */
	if (!fu_mtd_device_verify(self, chunks, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static GBytes *
fu_mtd_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMtdDevice *self = FU_MTD_DEVICE(device);
	gsize bufsz = fu_device_get_firmware_size_max(device);
	g_autofree guint8 *buf = g_malloc0(bufsz);
	g_autoptr(GPtrArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);

	/* read each chunk */
	chunks = fu_chunk_array_mutable_new(buf, bufsz, 0x0, 0x0, 10 * 1024);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		if (!fu_udev_device_pread(FU_UDEV_DEVICE(self),
					  fu_chunk_get_address(chk),
					  fu_chunk_get_data_out(chk),
					  fu_chunk_get_data_sz(chk),
					  error)) {
			g_prefix_error(error,
				       "failed to read @0x%x: ",
				       (guint)fu_chunk_get_address(chk));
			return NULL;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return g_bytes_new_take(g_steal_pointer(&buf), bufsz);
}

static gboolean
fu_mtd_device_write_firmware(FuDevice *device,
			     FuFirmware *firmware,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuMtdDevice *self = FU_MTD_DEVICE(device);
	gsize streamsz = 0;
	g_autoptr(GInputStream) stream = NULL;

	/* get data to write */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz > fu_device_get_firmware_size_max(device)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware too large, got 0x%x, expected <= 0x%x",
			    (guint)streamsz,
			    (guint)fu_device_get_firmware_size_max(device));
		return FALSE;
	}

	/* just one step required */
	if (self->erasesize == 0)
		return fu_mtd_device_write_verify(self, stream, progress, error);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 50, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, NULL);

	/* erase */
	if (!fu_mtd_device_erase(self, stream, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	if (!fu_mtd_device_write_verify(self, stream, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_mtd_device_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuMtdDevice *self = FU_MTD_DEVICE(device);

	/* load from quirks */
	if (g_strcmp0(key, "MtdMetadataOffset") == 0) {
		return fu_strtoull(value,
				   &self->metadata_offset,
				   0x0,
				   G_MAXUINT32,
				   FU_INTEGER_BASE_AUTO,
				   error);
	}
	if (g_strcmp0(key, "MtdMetadataSize") == 0) {
		return fu_strtoull(value,
				   &self->metadata_size,
				   0x100,
				   FU_FIRMWARE_SEARCH_MAGIC_BUFSZ_MAX,
				   FU_INTEGER_BASE_AUTO,
				   error);
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_mtd_device_init(FuMtdDevice *self)
{
	self->metadata_size = FU_FIRMWARE_SEARCH_MAGIC_BUFSZ_MAX;
	fu_device_set_summary(FU_DEVICE(self), "Memory Technology Device");
	fu_device_add_protocol(FU_DEVICE(self), "org.infradead.mtd");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_ICON);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_NAME);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_SIGNED);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_VENDOR);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_VERFMT);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_DRIVE_SSD);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_SYNC);
}

static void
fu_mtd_device_class_init(FuMtdDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->open = fu_mtd_device_open;
	device_class->probe = fu_mtd_device_probe;
	device_class->setup = fu_mtd_device_setup;
	device_class->to_string = fu_mtd_device_to_string;
	device_class->convert_version = fu_mtd_device_convert_version;
	device_class->dump_firmware = fu_mtd_device_dump_firmware;
	device_class->read_firmware = fu_mtd_device_read_firmware;
	device_class->write_firmware = fu_mtd_device_write_firmware;
	device_class->set_quirk_kv = fu_mtd_device_set_quirk_kv;
}
