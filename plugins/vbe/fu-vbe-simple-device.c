/*
 * VBE plugin for fwupd,vbe-simple
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libfdt.h>
#include <linux/fs.h>
#include <stdio.h>
#include <sys/ioctl.h>

#include "fit.h"
#include "fu-dfu-common.h"
#include "fu-vbe-simple-device.h"

/* G_OBJECT properties associated with the plugin */
enum { PROP_0, PROP_DEVNAME, PROP_AREA_START, PROP_AREA_SIZE, PROP_LAST };

/**
 * struct vbe_simple_state - current state of this VBE method
 *
 * This simply records successful updates at present. There is no log of
 * failures.
 *
 * @finish_time: Time that the the last update happened, 0 if none
 * @cur_version: Currently installed version, NULL if none
 * @status: State of the last update (always "completed"), NULL if none
 */
struct vbe_simple_state {
	/* information about the last update */
	time_t finish_time;
	gchar *cur_version;
	gchar *status;
};

/**
 * struct _FuVbeSimpleDevice - Information for the 'simple' VBE device
 *
 * @parent_instance: FuVbeDevice parent device
 * @storage: Storage device name (e.g. "mmc1")
 * @devname: Device name (e.g. /dev/mmcblk1)
 * @area_start: Start offset of area for firmware
 * @area_size: Size of firmware area
 * @skip_offset: This allows an initial part of the image to be skipped when
 * writing. This means that the first part of the image is ignored, with just
 * the latter part being written. For example, if this is 0x200 then the first
 * 512 bytes of the image (which must be present in the image) are skipped and
 * the bytes after that are written to the store offset.
 * @fd: File descriptor, if the device is open
 * @vbe_fname: Filename of the VBE state file
 * @state: State of this update method
 */
struct _FuVbeSimpleDevice {
	FuVbeDevice parent_instance;
	const gchar *storage;
	gchar *devname;
	off_t area_start;
	off_t area_size;
	gint skip_offset;
	gint fd;
	gchar *vbe_fname;
	struct vbe_simple_state state;
};

G_DEFINE_TYPE(FuVbeSimpleDevice, fu_vbe_simple_device, FU_TYPE_VBE_DEVICE)

/**
 * trailing_strtoln_end() - Get the number suffix at the end of a string
 *
 * Decodes a string like "abc123" into its two parts so that the base string
 * "abc" and the trailing number "123" can be obtained.
 *
 * For this example, with end == NULL, the function returns 123 and *endp
 * returns a pointer to the '1' character.
 *
 * @str: String to parse
 * @end: End of string to parse, or NULL to parse the whole string
 * @endp: Returns a pointer to the first character of the numeric suffix. If
 *	no number is found, this is @end if not NULL, else its points to the
 *	string's nul terminator
 * Returns: the numeric value of the suffix, or -1 if none found
 */
static gint
trailing_strtoln_end(const gchar *str, const gchar *end, gchar const **endp)
{
	const gchar *p;

	if (!end)
		end = str + strlen(str);
	p = end - 1;
	if (p > str && isdigit(*p)) {
		do {
			if (!isdigit(p[-1])) {
				if (endp)
					*endp = p;
				return atoi(p);
			}
		} while (--p > str);
	}
	if (endp)
		*endp = end;

	return -1;
}

static gboolean
fu_vbe_simple_device_probe(FuDevice *self, GError **error)
{
	struct _FuVbeSimpleDevice *dev = FU_VBE_SIMPLE_DEVICE(self);
	FuVbeDevice *vdev;
	const void *fdt;
	gint devnum, len;
	gint node;

	g_return_val_if_fail(FU_IS_VBE_DEVICE(self), FALSE);
	vdev = FU_VBE_DEVICE(self);

	if (!FU_DEVICE_CLASS(fu_vbe_simple_device_parent_class)->probe(self, error))
		return FALSE;

	fdt = fu_vbe_device_get_fdt(vdev);
	node = fu_vbe_device_get_node(vdev);
	g_debug("Probing device %s, fdt=%p, node=%d", fu_vbe_device_get_method(vdev), fdt, node);
	dev->storage = fdt_getprop(fdt, node, "storage", &len);
	if (!dev->storage) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Missing 'storage' property");
		return FALSE;
	}

	/* sanity check */
	if (len > PATH_MAX) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "'storage' property exceeds maximum size");
		return FALSE;
	}

	/* if this is an absolute path, use it */
	if (*dev->storage == '/') {
		dev->devname = g_strdup(dev->storage);
	} else {
		const gchar *end;

		/* obtain the 1 from "mmc1" */
		devnum = trailing_strtoln_end(dev->storage, NULL, &end);
		if (devnum == -1) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Cannot parse 'storage' property '%s' - expect <dev><num>",
				    dev->storage);
			return FALSE;
		}
		len = end - dev->storage;

		if (!strncmp("mmc", dev->storage, len)) {
			dev->devname = g_strdup_printf("/dev/mmcblk%d", devnum);
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Unsupported 'storage' media '%s'",
				    dev->storage);
			return FALSE;
		}
	}
	dev->area_start = fit_get_u32(fdt, node, "area-start");
	dev->area_size = fit_get_u32(fdt, node, "area-size");
	if (dev->area_start < 0 || dev->area_size < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Invalid/missing area start / size (%#jx / %#jx)",
			    (uintmax_t)dev->area_start,
			    (uintmax_t)dev->area_size);
		return FALSE;
	}

	/*
	 * We allow the skip offset to skip everything, which could be useful
	 * for testing
	 */
	dev->skip_offset = fit_get_u32(fdt, node, "skip-offset");
	if (dev->skip_offset > dev->area_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Store offset %#x is larger than size (%jx)",
			    (guint)dev->skip_offset,
			    (uintmax_t)dev->area_size);
		return FALSE;
	} else if (dev->skip_offset < 0) {
		dev->skip_offset = 0;
	}

	g_debug("Selected device '%s', start %#jx, size %#jx",
		dev->devname,
		(uintmax_t)dev->area_start,
		(uintmax_t)dev->area_size);

	return TRUE;
}

static gboolean
fu_vbe_simple_device_open(FuDevice *device, GError **error)
{
	struct _FuVbeSimpleDevice *dev = FU_VBE_SIMPLE_DEVICE(device);
	struct vbe_simple_state *state = &dev->state;
	g_autofree gchar *buf = NULL;
	gsize len;

	dev->fd = open(dev->devname, O_RDWR);
	if (dev->fd == -1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Cannot open file '%s' (%s)",
			    dev->devname,
			    strerror(errno));
		return FALSE;
	}

	if (g_file_get_contents(dev->vbe_fname, &buf, &len, NULL)) {
		gint node;

		node = fdt_subnode_offset(buf, 0, "last-update");
		state->finish_time = fit_get_u64(buf, node, "finish-time");
		state->cur_version = g_strdup(fdt_getprop(buf, node, "cur-version", NULL));
		state->status = g_strdup(fdt_getprop(buf, node, "status", NULL));
	} else {
		g_debug("No state file '%s' - will create", dev->vbe_fname);
		memset(state, '\0', sizeof(*state));
	}

	return TRUE;
}

static gboolean
fu_vbe_simple_device_close(FuDevice *device, GError **error)
{
	struct _FuVbeSimpleDevice *dev = FU_VBE_SIMPLE_DEVICE(device);
	struct vbe_simple_state *state = &dev->state;
	g_autofree gchar *buf = NULL;
	const gint size = 1024;

	close(dev->fd);
	dev->fd = -1;

	buf = g_malloc(size);
	fdt_create(buf, size);
	fdt_finish_reservemap(buf);

	fdt_begin_node(buf, "");
	fdt_property_string(buf, "compatible", "vbe");
	fdt_property_string(buf, "vbe,driver", "fwupd,vbe-simple");

	fdt_begin_node(buf, "last-update");
	fdt_property_u64(buf, "finish-time", state->finish_time);
	if (state->cur_version)
		fdt_property_string(buf, "cur-version", state->cur_version);
	if (state->status)
		fdt_property_string(buf, "status", state->status);
	fdt_end_node(buf);

	fdt_finish(buf);

	if (fdt_totalsize(buf) > (guint)size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BROKEN_SYSTEM,
			    "VBE state is too large (%#x with limit of %#x)",
			    fdt_totalsize(buf),
			    (guint)size);
		return FALSE;
	}

	if (!g_file_set_contents(dev->vbe_fname, buf, size, error)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BROKEN_SYSTEM,
			    "Unable to write VBE state");
		return FALSE;
	}

	g_free(state->cur_version);
	state->cur_version = NULL;
	g_free(state->status);
	state->status = NULL;

	return TRUE;
}

/**
 * check_config_match() - Check if this config is compatible with this model
 *
 * @fit: FIT to check
 * @cfg: Config node in FIT to check
 * @compat_list: List of compatible properties for this model, if any
 * Returns: 0 if the given cfg matches, -ve if not
 */
static gint
check_config_match(struct fit_info *fit, gint cfg, GList *compat_list)
{
	const GList *entry;
	gint prio;

	for (prio = 0, entry = g_list_first(compat_list); entry;
	     prio++, entry = g_list_next(entry)) {
		const gchar *compat, *p, *pend;
		const gchar *cmp = entry->data;
		gint ret, len;

		compat = fdt_getprop(fit->blob, cfg, "compatible", &len);
		g_debug("compat compare with: %s", cmp);
		if (!compat) {
			g_debug("   (none)");
		} else {
			for (p = compat, pend = compat + len; p < pend; p += strlen(p) + 1)
				g_debug("   %s", p);
		}
		ret = fdt_node_check_compatible(fit->blob, cfg, cmp);
		if (!ret || ret == -FDT_ERR_NOTFOUND)
			return prio;
	}

	return -1;
}

/**
 * process_image() - Write a single image to the device
 *
 * Look up an image node, read the data out of it and write the data to the
 * device at the correct store offset.
 *
 * @fit: FIT to write
 * @img: FIT offset of image to write
 * @dev: Device to use
 * @error: Returns error information if this function returns FALSE
 * Returns: TRUE on success, FALSE on failure
 */
static gboolean
process_image(struct fit_info *fit, gint img, struct _FuVbeSimpleDevice *dev, GError **error)
{
	guint store_offset = 0;
	const gchar *buf;
	off_t seek_to;
	gint size;
	gint ret;

	ret = fit_img_store_offset(fit, img);
	if (ret >= 0) {
		store_offset = ret;
	} else if (ret != -FITE_NOT_FOUND) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "Image '%s' store offset is invalid (%d)",
			    fit_img_name(fit, img),
			    ret);
		return FALSE;
	}

	buf = fit_img_data(fit, img, &size);
	if (!buf) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "Image '%s' data could not be read (%d)",
			    fit_img_name(fit, img),
			    size);
		return FALSE;
	}

	if (store_offset + size > dev->area_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "Image '%s' store_offset=%#x, size=%#x, area_size=%#jx",
			    fit_img_name(fit, img),
			    (guint)store_offset,
			    (guint)size,
			    (uintmax_t)dev->area_size);
		return FALSE;
	}

	if (dev->skip_offset >= size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "Image '%s' skip_offset=%#x, size=%#x, area_size=%#jx",
			    fit_img_name(fit, img),
			    (guint)store_offset,
			    (guint)size,
			    (uintmax_t)dev->area_size);
		return FALSE;
	}

	seek_to = dev->area_start + store_offset + dev->skip_offset;
	g_debug("Writing image '%s' size %x (skipping %x) to store_offset %x, seek %jx\n",
		fit_img_name(fit, img),
		(guint)size,
		(guint)dev->skip_offset,
		store_offset,
		(uintmax_t)seek_to);

	ret = lseek(dev->fd, seek_to, SEEK_SET);
	if (ret < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "Cannot seek file '%s' (%d) to %#jx (%s)",
			    dev->devname,
			    dev->fd,
			    (uintmax_t)seek_to,
			    strerror(errno));
		return FALSE;
	}

	ret = write(dev->fd, buf + dev->skip_offset, size - dev->skip_offset);
	if (ret < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "Cannot write file '%s' (%s)",
			    dev->devname,
			    strerror(errno));
		return FALSE;
	}

	return TRUE;
}

/**
 * process_config() - Write a single configuration to the device
 *
 * Look up all the images in a configuration and write them one by one to
 * the device.
 *
 * @fit: FIT to write
 * @cfg: FIT offset of configuration to write
 * @dev: Device to use
 * @progress: Progress information to update (this is simplistic and assumes
 * that each image takes the same time to write)
 * @error: Returns error information if this function returns FALSE
 * Returns: TRUE on success, FALSE on failure
 */
static gboolean
process_config(struct fit_info *fit,
	       gint cfg,
	       struct _FuVbeSimpleDevice *dev,
	       FuProgress *progress,
	       GError **error)
{
	gint count, i;

	count = fit_cfg_img_count(fit, cfg, "firmware");

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");

	for (i = 0; i < count; i++) {
		gint image = fit_cfg_img(fit, cfg, "firmware", i);

		if (image < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "'firmware' image #%d has no node",
				    i);
			return FALSE;
		}
		fu_progress_set_percentage_full(progress, i, count);

		if (!process_image(fit, image, dev, error))
			return FALSE;
	}
	fu_progress_set_percentage_full(progress, i, count);
	fu_progress_step_done(progress);

	return TRUE;
}

/**
 * process_fit() - Write the firmware from a FIT to the device
 *
 * Select the best configuration for the model, then write that configuration
 * to the device.
 *
 * @fit: FIT to write
 * @dev: Device to use
 * @compat_list: List of compatible properties for this model, if any
 * @progress: Progress information to update (this is simplistic and assumes
 * that each image takes the same time to write)
 * @error: Returns error information if this function returns FALSE
 * Returns: TRUE on success, FALSE on failure
 */
static gboolean
process_fit(struct fit_info *fit,
	    struct _FuVbeSimpleDevice *dev,
	    GList *compat_list,
	    FuProgress *progress,
	    GError **error)
{
	struct vbe_simple_state *state = &dev->state;
	gint best_prio = INT_MAX;
	const gchar *cfg_name;
	const gchar *version;
	const GList *entry;
	gint cfg_count = 0;
	gint best_cfg = 0;
	gint cfg;

	g_debug("model: ");
	for (entry = g_list_first(compat_list); entry; entry = g_list_next(entry))
		g_debug("   %s", (gchar *)entry->data);

	for (cfg = fit_first_cfg(fit); cfg > 0; cfg_count++, cfg = fit_next_cfg(fit, cfg)) {
		gint prio = check_config_match(fit, cfg, compat_list);
		g_debug("config '%s': priority=%d", fit_cfg_name(fit, cfg), prio);
		if (prio >= 0 && (!best_cfg || prio < best_prio)) {
			best_cfg = cfg;
			best_prio = prio;
		}
	}

	g_debug("cfg_count=%d, best_cfg=%d", cfg_count, best_cfg);
	if (!cfg_count) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_READ, "FIT has no configurations");
		return FALSE;
	}

	if (!best_cfg) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_READ, "No matching configuration");
		return FALSE;
	}
	cfg_name = fit_cfg_name(fit, best_cfg);
	version = fit_cfg_version(fit, best_cfg);
	if (!version) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "Configuration '%s' has no version",
			    cfg_name);
		return FALSE;
	}

	g_debug("Best configuration: '%s', priority %d, version %s", cfg_name, best_prio, version);

	if (!process_config(fit, best_cfg, dev, progress, error))
		return FALSE;

	g_free(state->cur_version);
	g_free(state->status);
	state->finish_time = time(NULL);
	state->cur_version = g_strdup(version);
	state->status = g_strdup("completed");

	return TRUE;
}

static gboolean
fu_vbe_simple_device_write_firmware(FuDevice *self,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	struct _FuVbeSimpleDevice *dev = FU_VBE_SIMPLE_DEVICE(self);
	g_autoptr(GBytes) bytes = NULL;
	struct fit_info fit;
	GList *compat_list;
	FuVbeDevice *vdev;
	const guint8 *buf;
	gsize size = 0;
	gint ret;

	g_return_val_if_fail(FU_IS_VBE_DEVICE(self), FALSE);
	vdev = FU_VBE_DEVICE(self);

	compat_list = fu_vbe_device_get_compat_list(vdev);

	bytes = fu_firmware_get_bytes(firmware, error);
	if (!bytes)
		return FALSE;
	buf = g_bytes_get_data(bytes, &size);

	g_debug("Size of FIT: %#zx\n", size);
	ret = fit_open(&fit, buf, size);
	if (ret) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "Failed to open FIT: %s",
			    fit_strerror(ret));
		return FALSE;
	}

	if (!process_fit(&fit, dev, compat_list, progress, error))
		return FALSE;

	/* success */
	return TRUE;
}

static GBytes *
fu_vbe_simple_device_upload(FuDevice *self, FuProgress *progress, GError **error)
{
	struct _FuVbeSimpleDevice *dev = FU_VBE_SIMPLE_DEVICE(self);
	g_autoptr(GPtrArray) chunks = NULL;
	gsize blksize = 0x100000;
	gpointer buf;
	GBytes *out;
	off_t offset;
	gint ret;

	/* notify UI */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);

	ret = lseek(dev->fd, dev->area_start, SEEK_SET);
	if (ret < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "Cannot seek file '%s' (%d) to %#jx (%s)",
			    dev->devname,
			    dev->fd,
			    (uintmax_t)dev->area_start,
			    strerror(errno));
		return NULL;
	}

	chunks = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);

	for (offset = 0; offset < dev->area_size; offset += blksize) {
		g_autoptr(GBytes) chunk = NULL;
		gsize toread;

		buf = g_malloc(blksize);
		toread = blksize;
		if ((off_t)toread + offset > dev->area_size)
			toread = dev->area_size - offset;
		g_debug("read %zx", toread);
		ret = read(dev->fd, buf, toread);
		if (ret < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "Cannot read file '%s' (%s)",
				    dev->devname,
				    strerror(errno));
			g_free(buf);
			return NULL;
		}
		chunk = g_bytes_new_take(buf, ret);
		g_ptr_array_add(chunks, g_steal_pointer(&chunk));
	}

	out = fu_dfu_utils_bytes_join_array(chunks);
	g_debug("Total bytes read from device: %#zx\n", g_bytes_get_size(out));

	return out;
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
	fu_device_add_guid(FU_DEVICE(self), "bb3b05a8-ebef-11ec-be98-d3a15278be95");
	fu_device_set_vendor(FU_DEVICE(self), "U-Boot");
	fu_device_add_vendor_id(FU_DEVICE(self), "VBE:U-Boot");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version_lowest(FU_DEVICE(self), "0.0.1");
}

FuDevice *
fu_vbe_simple_device_new(FuContext *ctx,
			 const gchar *vbe_method,
			 const gchar *fdt,
			 gint node,
			 const gchar *vbe_dir)
{
	return FU_DEVICE(g_object_new(FU_TYPE_VBE_SIMPLE_DEVICE,
				      "context",
				      ctx,
				      "vbe-method",
				      vbe_method,
				      "fdt",
				      fdt,
				      "node",
				      node,
				      "vbe-dir",
				      vbe_dir,
				      NULL));
}

static void
fu_vbe_simple_device_constructed(GObject *obj)
{
	struct _FuVbeSimpleDevice *dev = FU_VBE_SIMPLE_DEVICE(obj);
	g_autofree gchar *vbe_fname = NULL;
	FuVbeDevice *vdev;

	g_return_if_fail(FU_IS_VBE_DEVICE(dev));
	vdev = FU_VBE_DEVICE(dev);

	vbe_fname = g_build_filename(fu_vbe_device_get_dir(vdev), "simple.dtb", NULL);
	dev->vbe_fname = g_steal_pointer(&vbe_fname);
}

static void
fu_vbe_simple_device_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuVbeSimpleDevice *self = FU_VBE_SIMPLE_DEVICE(obj);
	switch (prop_id) {
	case PROP_DEVNAME:
		g_value_set_string(value, self->devname);
		break;
	case PROP_AREA_START:
		g_value_set_int64(value, self->area_start);
		break;
	case PROP_AREA_SIZE:
		g_value_set_int64(value, self->area_size);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
fu_vbe_simple_device_set_property(GObject *obj,
				  guint prop_id,
				  const GValue *value,
				  GParamSpec *pspec)
{
	FuVbeSimpleDevice *self = FU_VBE_SIMPLE_DEVICE(obj);
	switch (prop_id) {
	case PROP_DEVNAME:
		if (self->devname)
			g_free(self->devname);
		self->devname = g_strdup(g_value_get_string(value));
		break;
	case PROP_AREA_START:
		self->area_start = g_value_get_int64(value);
		break;
	case PROP_AREA_SIZE:
		self->area_size = g_value_get_int64(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
fu_vbe_simple_device_finalize(GObject *obj)
{
	FuVbeSimpleDevice *self = FU_VBE_SIMPLE_DEVICE(obj);
	if (self->devname)
		g_free(self->devname);

	G_OBJECT_CLASS(fu_vbe_simple_device_parent_class)->finalize(obj);
}

static void
fu_vbe_simple_device_class_init(FuVbeSimpleDeviceClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	object_class->get_property = fu_vbe_simple_device_get_property;
	object_class->set_property = fu_vbe_simple_device_set_property;

	/**
	 * FuVbeSimpleDevice:devname:
	 *
	 * device that contains firmware (e.g. '/dev/mmcblk1')
	 */
	pspec =
	    g_param_spec_string("devname",
				NULL,
				"Device that contains firmware (e.g. '/dev/mmcblk1')",
				NULL,
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_DEVNAME, pspec);
	/**
	 * FuVbeSimpleDevice:area-start:
	 *
	 * start offset of area for firmware
	 */
	pspec =
	    g_param_spec_int64("area-start",
			       NULL,
			       "Start offset of area for firmware",
			       -1,
			       INT_MAX,
			       -1,
			       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_AREA_START, pspec);

	/**
	 * FuVbeSimpleDevice:area-size:
	 *
	 * size of firmware area
	 */
	pspec =
	    g_param_spec_int64("area-size",
			       NULL,
			       "Size of firmware area",
			       -1,
			       INT_MAX,
			       -1,
			       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_AREA_SIZE, pspec);

	object_class->constructed = fu_vbe_simple_device_constructed;
	object_class->finalize = fu_vbe_simple_device_finalize;
	klass_device->probe = fu_vbe_simple_device_probe;
	klass_device->open = fu_vbe_simple_device_open;
	klass_device->close = fu_vbe_simple_device_close;
	klass_device->set_progress = fu_vbe_simple_device_set_progress;
	klass_device->write_firmware = fu_vbe_simple_device_write_firmware;
	klass_device->dump_firmware = fu_vbe_simple_device_upload;
}
