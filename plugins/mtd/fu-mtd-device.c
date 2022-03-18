/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_MTD_USER_H
#include <mtd/mtd-user.h>
#endif

#include "fu-mtd-device.h"

struct _FuMtdDevice {
	FuUdevDevice parent_instance;
	guint64 erasesize;
};

G_DEFINE_TYPE(FuMtdDevice, fu_mtd_device, FU_TYPE_UDEV_DEVICE)

static void
fu_mtd_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuMtdDevice *self = FU_MTD_DEVICE(device);
	if (self->erasesize > 0)
		fu_common_string_append_kx(str, idt, "EraseSize", self->erasesize);
}

static gboolean
fu_mtd_device_setup(FuDevice *device, GError **error)
{
	GType firmware_gtype = fu_device_get_firmware_gtype(device);
	const gchar *fn;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GFile) file = NULL;

	/* nothing to do */
	if (firmware_gtype == G_TYPE_INVALID)
		return TRUE;

	/* read entire contents */
	fn = fu_udev_device_get_device_file(FU_UDEV_DEVICE(device));
	if (fn == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Not supported as no device file");
		return FALSE;
	}
	file = g_file_new_for_path(fn);
	firmware = g_object_new(firmware_gtype, NULL);
	if (!fu_firmware_parse_file(firmware, file, FWUPD_INSTALL_FLAG_NONE, error))
		return FALSE;

	/* copy over the version */
	if (fu_firmware_get_version(firmware) != NULL)
		fu_device_set_version(device, fu_firmware_get_version(firmware));
	if (fu_firmware_get_version_raw(firmware) != G_MAXUINT64)
		fu_device_set_version_raw(device, fu_firmware_get_version_raw(firmware));

	/* success */
	return TRUE;
}

static gboolean
fu_mtd_device_open(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* FuUdevDevice->open */
	if (!FU_DEVICE_CLASS(fu_mtd_device_parent_class)->open(device, &error_local)) {
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

	/* success */
	return TRUE;
}

static gboolean
fu_mtd_device_probe(FuDevice *device, GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	FuMtdDevice *self = FU_MTD_DEVICE(device);
	const gchar *name;
	const gchar *vendor;
	guint64 flags = 0;
	guint64 size = 0;
	g_autoptr(GError) error_local = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_mtd_device_parent_class)->probe(device, error))
		return FALSE;

	/* set physical ID */
	if (!fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "mtd", error))
		return FALSE;

	/* flags have to exist */
	if (!fu_udev_device_get_sysfs_attr_uint64(FU_UDEV_DEVICE(device),
						  "flags",
						  &flags,
						  &error_local)) {
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "no MTD flags");
			return FALSE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* get name */
	name = fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(device), "name", NULL);
	if (name != NULL)
		fu_device_set_name(FU_DEVICE(self), name);

	/* set vendor ID as the BIOS vendor */
	vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_MANUFACTURER);
	if (vendor != NULL) {
		g_autofree gchar *vendor_id = g_strdup_printf("DMI:%s", vendor);
		fu_device_add_vendor_id(device, vendor_id);
	}

	/* use vendor and product as an optional instance ID prefix */
	fu_device_add_instance_strsafe(device, "NAME", name);
	fu_device_add_instance_strsafe(device, "VENDOR", vendor);
	fu_device_add_instance_strsafe(device,
				       "PRODUCT",
				       fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_PRODUCT_NAME));
	fu_device_build_instance_id(device, NULL, "MTD", "NAME", NULL);
	fu_device_build_instance_id(device, NULL, "MTD", "VENDOR", "NAME", NULL);
	fu_device_build_instance_id(device, NULL, "MTD", "VENDOR", "PRODUCT", "NAME", NULL);

	/* get properties about the device */
	if (!fu_udev_device_get_sysfs_attr_uint64(FU_UDEV_DEVICE(device), "size", &size, error))
		return FALSE;
	fu_device_set_firmware_size_max(device, size);
#ifdef HAVE_MTD_USER_H
	if ((flags & MTD_NO_ERASE) == 0) {
		if (!fu_udev_device_get_sysfs_attr_uint64(FU_UDEV_DEVICE(device),
							  "erasesize",
							  &self->erasesize,
							  error))
			return FALSE;
	}
	if (flags & MTD_WRITEABLE)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
#endif

	/* success */
	return TRUE;
}

static gboolean
fu_mtd_device_erase(FuMtdDevice *self, GBytes *fw, FuProgress *progress, GError **error)
{
#ifdef HAVE_MTD_USER_H
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, self->erasesize);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);

	/* erase each chunk */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		struct erase_info_user erase = {
		    .start = fu_chunk_get_address(chk),
		    .length = fu_chunk_get_data_sz(chk),
		};
		if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self), 2, (guint8 *)&erase, NULL, error)) {
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
fu_mtd_device_write(FuMtdDevice *self, GPtrArray *chunks, FuProgress *progress, GError **error)
{
	/* progress */
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
		if (!fu_udev_device_pwrite_full(FU_UDEV_DEVICE(self),
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
fu_mtd_device_verify(FuMtdDevice *self, GPtrArray *chunks, FuProgress *progress, GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);

	/* verify each chunk */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autofree guint8 *buf = g_malloc0(fu_chunk_get_data_sz(chk));
		g_autoptr(GBytes) blob1 = fu_chunk_get_bytes(chk);
		g_autoptr(GBytes) blob2 = NULL;

		if (!fu_udev_device_pread_full(FU_UDEV_DEVICE(self),
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
		if (!fu_common_bytes_compare(blob1, blob2, error)) {
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
fu_mtd_device_write_verify(FuMtdDevice *self, GBytes *fw, FuProgress *progress, GError **error)
{
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, 10 * 1024);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 50);

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
		if (!fu_udev_device_pread_full(FU_UDEV_DEVICE(self),
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
	g_autoptr(GBytes) fw = NULL;

	/* get data to write */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	if (g_bytes_get_size(fw) > fu_device_get_firmware_size_max(device)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware too large, got 0x%x, expected <= 0x%x",
			    (guint)g_bytes_get_size(fw),
			    (guint)fu_device_get_firmware_size_max(device));
		return FALSE;
	}

	/* just one step required */
	if (self->erasesize == 0)
		return fu_mtd_device_write_verify(self, fw, progress, error);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 50);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50);

	/* erase */
	if (!fu_mtd_device_erase(self, fw, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	if (!fu_mtd_device_write_verify(self, fw, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_mtd_device_init(FuMtdDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self), "Memory Technology Device");
	fu_device_add_protocol(FU_DEVICE(self), "org.infradead.mtd");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_SIGNED);
	fu_device_add_icon(FU_DEVICE(self), "drive-harddisk-solidstate");
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
				 FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
				     FU_UDEV_DEVICE_FLAG_OPEN_SYNC);
}

static void
fu_mtd_device_class_init(FuMtdDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->open = fu_mtd_device_open;
	klass_device->probe = fu_mtd_device_probe;
	klass_device->setup = fu_mtd_device_setup;
	klass_device->to_string = fu_mtd_device_to_string;
	klass_device->dump_firmware = fu_mtd_device_dump_firmware;
	klass_device->write_firmware = fu_mtd_device_write_firmware;
}
