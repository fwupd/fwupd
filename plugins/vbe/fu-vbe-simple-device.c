/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <fcntl.h>
#include <stdio.h>

#include "fu-vbe-simple-device.h"

/**
 * @skip_offset: This allows an initial part of the image to be skipped when
 * writing. This means that the first part of the image is ignored, with just
 * the latter part being written. For example, if this is 0x200 then the first
 * 512 bytes of the image (which must be present in the image) are skipped and
 * the bytes after that are written to the store offset.
 */
struct _FuVbeSimpleDevice {
	FuVbeDevice parent_instance;
	gchar *storage; /* e.g. "mmc1" */
	gchar *devname; /* e.g. /dev/mmcblk1 */
	guint32 area_start;
	guint32 area_size;
	guint32 skip_offset;
	gint fd;
};

G_DEFINE_TYPE(FuVbeSimpleDevice, fu_vbe_simple_device, FU_TYPE_VBE_DEVICE)

static void
fu_vbe_simple_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuVbeSimpleDevice *self = FU_VBE_SIMPLE_DEVICE(device);

	/* FuVbeDevice->to_string */
	FU_DEVICE_CLASS(fu_vbe_simple_device_parent_class)->to_string(device, idt, str);

	if (self->storage != NULL)
		fu_string_append(str, idt, "Storage", self->storage);
	if (self->devname != NULL)
		fu_string_append(str, idt, "Devname", self->devname);
	fu_string_append_kx(str, idt, "AreaStart", self->area_start);
	fu_string_append_kx(str, idt, "AreaSize", self->area_size);
	if (self->skip_offset != 0)
		fu_string_append_kx(str, idt, "SkipOffset", self->skip_offset);
}

static gboolean
fu_vbe_simple_device_parse_devnum(const gchar *str, guint *value, GError **error)
{
	guint64 val64 = 0;

	/* skip non-numeric part */
	while (*str != '\0' && g_ascii_isdigit(*str))
		str++;

	/* convert to uint */
	if (!fu_strtoull(str, &val64, 0x0, G_MAXUINT, error))
		return FALSE;
	if (value != NULL)
		*value = val64;
	return TRUE;
}

static gboolean
fu_vbe_simple_device_probe(FuDevice *device, GError **error)
{
	FuVbeSimpleDevice *self = FU_VBE_SIMPLE_DEVICE(device);
	FuFdtImage *fdt_node;

	g_return_val_if_fail(FU_IS_VBE_DEVICE(self), FALSE);

	/* FuVbeDevice->probe */
	if (!FU_DEVICE_CLASS(fu_vbe_simple_device_parent_class)->probe(device, error))
		return FALSE;

	fdt_node = fu_vbe_device_get_fdt_node(FU_VBE_DEVICE(self));
	if (!fu_fdt_image_get_attr_str(fdt_node, "storage", &self->storage, error))
		return FALSE;

	/* if this is an absolute path, use it */
	if (g_str_has_prefix(self->storage, "/")) {
		self->devname = g_strdup(self->storage);
	} else {
		guint devnum = 0;

		/* obtain the 1 from "mmc1" */
		if (!fu_vbe_simple_device_parse_devnum(self->storage, &devnum, error)) {
			g_prefix_error(error, "cannot parse storage property %s: ", self->storage);
			return FALSE;
		}
		if (g_str_has_prefix(self->storage, "mmc")) {
			self->devname = g_strdup_printf("/dev/mmcblk%u", devnum);
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported 'storage' media '%s'",
				    self->storage);
			return FALSE;
		}
	}

	/* get area */
	if (!fu_fdt_image_get_attr_u32(fdt_node, "area-start", &self->area_start, error))
		return FALSE;
	if (!fu_fdt_image_get_attr_u32(fdt_node, "area-size", &self->area_size, error))
		return FALSE;

	/* an optional skip offset to skip everything, which could be useful for testing */
	fu_fdt_image_get_attr_u32(fdt_node, "skip-offset", &self->skip_offset, NULL);
	if (self->skip_offset > self->area_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "store offset 0x%x is larger than size 0x%x",
			    (guint)self->skip_offset,
			    (guint)self->area_size);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_vbe_simple_device_open(FuDevice *device, GError **error)
{
	FuVbeSimpleDevice *self = FU_VBE_SIMPLE_DEVICE(device);

	/* open device */
	self->fd = open(self->devname, O_RDWR);
	if (self->fd == -1) {
#ifdef HAVE_ERRNO_H
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot open %s [%s]",
			    self->devname,
			    strerror(errno));
#else
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot open %s",
			    self->devname);
		return FALSE;
#endif
	}

	/* success */
	return TRUE;
}

static gboolean
fu_vbe_simple_device_close(FuDevice *device, GError **error)
{
	FuVbeSimpleDevice *self = FU_VBE_SIMPLE_DEVICE(device);

	/* close device */
	close(self->fd);
	self->fd = -1;

	/* success */
	return TRUE;
}

static FuFdtImage *
fu_vbe_simple_device_get_cfg_compatible(FuVbeSimpleDevice *self,
					FuFirmware *firmware,
					GError **error)
{
	gchar **device_compatible;
	g_autoptr(FuFdtImage) fdt_configurations = NULL;
	g_autoptr(GPtrArray) img_configurations = NULL;
	g_autofree gchar *str = NULL;

	/* get all configurations */
	fdt_configurations =
	    fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(firmware),
					      "/" FU_FIT_FIRMWARE_ID_CONFIGURATIONS,
					      error);
	if (fdt_configurations == NULL)
		return NULL;
	img_configurations = fu_firmware_get_images(firmware);

	/* look for a configuration with the device compatible strings in priority order */
	device_compatible = fu_vbe_device_get_compatible(FU_VBE_DEVICE(self));
	for (guint j = 0; device_compatible[j] != NULL; j++) {
		for (guint i = 0; i < img_configurations->len; i++) {
			FuFdtImage *img = g_ptr_array_index(img_configurations, i);
			g_auto(GStrv) img_compatible = NULL;
			if (!fu_fdt_image_get_attr_strlist(img,
							   FU_FIT_FIRMWARE_ATTR_COMPATIBLE,
							   &img_compatible,
							   error))
				return NULL;
			if (g_strv_contains((const gchar *const *)img_compatible,
					    device_compatible[j]))
				return g_object_ref(img);
		}
	}

	/* failure */
	str = g_strjoinv(", ", device_compatible);
	g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no images found that match %s", str);
	return NULL;
}

static FuFirmware *
fu_vbe_simple_device_prepare_firmware(FuDevice *device,
				      GBytes *fw,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuVbeSimpleDevice *self = FU_VBE_SIMPLE_DEVICE(device);
	g_autofree gchar *version = NULL;
	g_auto(GStrv) firmware_ids = NULL;
	g_autoptr(FuFdtImage) img_cfg = NULL;
	g_autoptr(FuFirmware) firmware = fu_fit_firmware_new();
	g_autoptr(FuFirmware) firmware_container = fu_firmware_new();

	/* parse all images */
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;

	/* look for a compatible configuration */
	img_cfg = fu_vbe_simple_device_get_cfg_compatible(self, firmware, error);
	if (img_cfg == NULL)
		return NULL;
	if (!fu_fdt_image_get_attr_str(img_cfg, FU_FIT_FIRMWARE_ATTR_VERSION, &version, error))
		return NULL;

	/* check the firmware images exists */
	if (!fu_fdt_image_get_attr_strlist(img_cfg, "firmware", &firmware_ids, error))
		return NULL;
	for (guint i = 0; firmware_ids[i] != NULL; i++) {
		g_autofree gchar *path = NULL;
		g_autoptr(FuFdtImage) img_firmware = NULL;
		path = g_strdup_printf("/%s/%s", FU_FIT_FIRMWARE_ID_IMAGES, firmware_ids[i]);
		img_firmware =
		    fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(firmware), path, error);
		if (img_firmware == NULL)
			return NULL;
		fu_firmware_add_image(firmware_container, FU_FIRMWARE(img_firmware));
	}

	/* success: return the container */
	return g_steal_pointer(&firmware_container);
}

static gboolean
fu_vbe_simple_device_write_firmware_img(FuVbeSimpleDevice *self,
					FuFdtImage *img,
					FuProgress *progress,
					GError **error)
{
	const guint8 *buf;
	gssize rc;
	gsize bufsz = 0;
	gsize seek_to;
	guint32 store_offset = 0;
	g_autoptr(GBytes) blob = NULL;

	/* get data */
	blob = fu_fdt_image_get_attr(img, FU_FIT_FIRMWARE_ATTR_DATA, error);
	if (blob == NULL)
		return FALSE;
	buf = g_bytes_get_data(blob, &bufsz);
	fu_fdt_image_get_attr_u32(img, "store-offset", &store_offset, NULL);

	/* sanity check */
	if (store_offset + bufsz > self->area_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "image '%s' store_offset=0x%x, bufsz=0x%x, area_size=0x%x",
			    fu_firmware_get_id(FU_FIRMWARE(img)),
			    (guint)store_offset,
			    (guint)bufsz,
			    (guint)self->area_size);
		return FALSE;
	}
	if (self->skip_offset >= bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "image '%s' skip_offset=0x%x, bufsz=0x%x, area_size=0x%x",
			    fu_firmware_get_id(FU_FIRMWARE(img)),
			    (guint)store_offset,
			    (guint)bufsz,
			    (guint)self->area_size);
		return FALSE;
	}

	/* seek to correct address */
	seek_to = self->area_start + store_offset + self->skip_offset;
	g_debug("writing image '%s' bufsz 0x%x (skipping 0x%x) to store_offset 0x%x, seek 0x%x\n",
		fu_firmware_get_id(FU_FIRMWARE(img)),
		(guint)bufsz,
		(guint)self->skip_offset,
		(guint)store_offset,
		(guint)seek_to);
	rc = lseek(self->fd, seek_to, SEEK_SET);
	if (rc < 0) {
#ifdef HAVE_ERRNO_H
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "cannot seek file '%s' to 0x%x [%s]",
			    self->devname,
			    (guint)seek_to,
			    strerror(errno));
#else
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "cannot seek file '%s' to 0x%x",
			    self->devname,
			    (guint)seek_to);
#endif
		return FALSE;
	}

	/* write buffer */
	rc = write(self->fd, buf + self->skip_offset, bufsz - self->skip_offset);
	if (rc < 0) {
#ifdef HAVE_ERRNO_H
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "cannot write file '%s' [%s]",
			    self->devname,
			    strerror(errno));
#else
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "cannot write file '%s'",
			    self->devname);
#endif
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_vbe_simple_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuVbeSimpleDevice *self = FU_VBE_SIMPLE_DEVICE(device);
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	g_return_val_if_fail(FU_IS_VBE_DEVICE(self), FALSE);

	/* write each firmware image */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, imgs->len);
	for (guint i = 0; i < imgs->len; i++) {
		FuFdtImage *img = g_ptr_array_index(imgs, i);
		if (!fu_vbe_simple_device_write_firmware_img(self,
							     img,
							     fu_progress_get_child(progress),
							     error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_vbe_simple_device_upload(FuDevice *device, FuProgress *progress, GError **error)
{
	FuVbeSimpleDevice *self = FU_VBE_SIMPLE_DEVICE(device);
	gssize rc;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* notify UI */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);

	/* seek to start */
	rc = lseek(self->fd, self->area_start, SEEK_SET);
	if (rc < 0) {
#ifdef HAVE_ERRNO_H
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "cannot seek file %s to 0x%x [%s]",
			    self->devname,
			    (guint)self->area_start,
			    strerror(errno));
#else
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "cannot seek file %s to 0x%x",
			    self->devname,
			    (guint)self->area_start);
#endif
		return NULL;
	}

	/* process in chunks */
	chunks = fu_chunk_array_new(NULL, self->area_size - self->area_start, 0x0, 0x0, 0x100000);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autofree guint8 *tmpbuf = g_malloc0(fu_chunk_get_data_sz(chk));

		rc = read(self->fd, tmpbuf, fu_chunk_get_data_sz(chk));
		if (rc != (gssize)fu_chunk_get_data_sz(chk)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "incomplete read of %s @0x%x",
				    self->devname,
				    fu_chunk_get_address(chk));
			return NULL;
		}
		g_byte_array_append(buf, tmpbuf, fu_chunk_get_data_sz(chk));
		fu_progress_step_done(progress);
	}

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_vbe_simple_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_vbe_simple_device_init(FuVbeSimpleDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "simple");
	fu_device_set_vendor(FU_DEVICE(self), "U-Boot");
	fu_device_add_vendor_id(FU_DEVICE(self), "VBE:U-Boot");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version_lowest(FU_DEVICE(self), "0.0.1");
}

static void
fu_vbe_simple_device_constructed(GObject *obj)
{
	FuVbeSimpleDevice *self = FU_VBE_SIMPLE_DEVICE(obj);
	fu_device_add_guid(FU_DEVICE(self), "bb3b05a8-ebef-11ec-be98-d3a15278be95");
}

static void
fu_vbe_simple_device_finalize(GObject *obj)
{
	FuVbeSimpleDevice *self = FU_VBE_SIMPLE_DEVICE(obj);
	g_free(self->devname);
	g_free(self->storage);
	G_OBJECT_CLASS(fu_vbe_simple_device_parent_class)->finalize(obj);
}

static void
fu_vbe_simple_device_class_init(FuVbeSimpleDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->constructed = fu_vbe_simple_device_constructed;
	object_class->finalize = fu_vbe_simple_device_finalize;
	klass_device->to_string = fu_vbe_simple_device_to_string;
	klass_device->probe = fu_vbe_simple_device_probe;
	klass_device->open = fu_vbe_simple_device_open;
	klass_device->close = fu_vbe_simple_device_close;
	klass_device->set_progress = fu_vbe_simple_device_set_progress;
	klass_device->prepare_firmware = fu_vbe_simple_device_prepare_firmware;
	klass_device->write_firmware = fu_vbe_simple_device_write_firmware;
	klass_device->dump_firmware = fu_vbe_simple_device_upload;
}
