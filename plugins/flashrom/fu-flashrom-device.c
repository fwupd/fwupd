/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Daniel Campello <campello@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libflashrom.h>

#include "fu-flashrom-cmos.h"
#include "fu-flashrom-device.h"

#define FU_FLASHROM_DEVICE_FLAG_RESET_CMOS     "reset-cmos"
#define FU_FLASHROM_DEVICE_FLAG_FN_M_ME_UNLOCK "fn-m-me-unlock"

struct _FuFlashromDevice {
	FuUdevDevice parent_instance;
	FuIfdRegion ifd_region;
	gchar *fmap_regions;
	struct flashrom_flashctx *flashctx;
	struct flashrom_layout *layout;
};

G_DEFINE_TYPE(FuFlashromDevice, fu_flashrom_device, FU_TYPE_UDEV_DEVICE)

enum { PROP_0, PROP_FLASHCTX, PROP_REGION, PROP_LAST };

static void
fu_flashrom_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(device);
	fwupd_codec_string_append(str, idt, "IfdRegion", fu_ifd_region_to_string(self->ifd_region));
	fwupd_codec_string_append(str, idt, "FmapRegions", self->fmap_regions);
}

static gboolean
fu_flashrom_device_set_quirk_kv(FuDevice *device,
				const gchar *key,
				const gchar *value,
				GError **error)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(device);
	if (g_strcmp0(key, "FlashromFmapRegions") == 0) {
		if (g_strcmp0(value, "") == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "FMAP regions cannot be empty");
			return FALSE;
		}
		g_free(self->fmap_regions);
		self->fmap_regions = g_strdup(value);
		return TRUE;
	}
	if (g_strcmp0(key, "PciBcrAddr") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		fu_device_set_metadata_integer(device, "PciBcrAddr", tmp);
		return TRUE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no supported");
	return FALSE;
}

static gboolean
fu_flashrom_device_probe(FuDevice *device, GError **error)
{
	g_autofree gchar *dev_name = NULL;
	const gchar *sysfs_path = NULL;

	sysfs_path = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	if (sysfs_path != NULL) {
		g_autofree gchar *physical_id = NULL;
		physical_id = g_strdup_printf("DEVNAME=%s", sysfs_path);
		fu_device_set_physical_id(device, physical_id);
	}
	dev_name = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
					     "name",
					     FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					     NULL);
	if (dev_name != NULL)
		fu_device_add_instance_id_full(device, dev_name, FU_DEVICE_INSTANCE_FLAG_QUIRKS);
	return TRUE;
}

typedef struct flashrom_layout _flashrom_layout;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(_flashrom_layout, flashrom_layout_release)

static gboolean
fu_flashrom_device_open_fmap(FuFlashromDevice *self, GError **error)
{
	g_auto(GStrv) fmap_regions = g_strsplit(self->fmap_regions, ",", 0);
	g_autoptr(_flashrom_layout) layout = NULL;

	if (flashrom_layout_read_fmap_from_rom(&layout, self->flashctx, 0, 0)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "failed to read layout from FMAP");
		return FALSE;
	}
	for (guint i = 0; fmap_regions[i] != NULL; i++)
		flashrom_layout_include_region(layout, fmap_regions[i]);

	/* does not transfer ownership, so we must manage the lifetime of layout */
	self->layout = g_steal_pointer(&layout);
	flashrom_layout_set(self->flashctx, self->layout);

	/* success */
	return TRUE;
}

static gboolean
fu_flashrom_device_open_ifd(FuFlashromDevice *self, GError **error)
{
	g_autoptr(_flashrom_layout) layout = NULL;

	if (flashrom_layout_read_from_ifd(&layout, self->flashctx, NULL, 0)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "failed to read layout from Intel ICH descriptor");
		return FALSE;
	}
	if (flashrom_layout_include_region(layout, fu_ifd_region_to_string(self->ifd_region))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "partition '%s' not found in IFD",
			    fu_ifd_region_to_string(self->ifd_region));
		return FALSE;
	}

	/* does not transfer ownership, so we must manage the lifetime of layout */
	self->layout = g_steal_pointer(&layout);
	flashrom_layout_set(self->flashctx, self->layout);

	/* success */
	return TRUE;
}

static gboolean
fu_flashrom_device_open(FuDevice *device, GError **error)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(device);

	/* sanity check */
	if (self->flashctx == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no flashctx");
		return FALSE;
	}

	/* get the flash size from the device if not already been quirked */
	if (fu_device_get_firmware_size_max(device) == 0) {
		gsize flash_size = flashrom_flash_getsize(self->flashctx);
		if (flash_size == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "flash size zero");
			return FALSE;
		}
		fu_device_set_firmware_size_max(device, flash_size);
	}

	/* either use IFD or FMAP */
	if (fu_cpu_get_vendor() == FU_CPU_VENDOR_INTEL) {
		if (self->fmap_regions != NULL) {
			if (!fu_flashrom_device_open_fmap(self, error))
				return FALSE;
		} else {
			if (!fu_flashrom_device_open_ifd(self, error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_flashrom_device_close(FuDevice *device, GError **error)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(device);

	if (self->layout != NULL) {
		flashrom_layout_release(self->layout);
		self->layout = NULL;

		flashrom_layout_set(self->flashctx, NULL);
	}

	return TRUE;
}

#ifdef HAVE_FLASHROM_SET_PROGRESS_CALLBACK_V2
static void
fu_flashrom_device_progress_cb(enum flashrom_progress_stage stage,
			       size_t current,
			       size_t total,
			       void *user_data)
{
	FuProgress *progress = FU_PROGRESS(user_data);

	/* status */
	if (stage == FLASHROM_PROGRESS_READ)
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
	else if (stage == FLASHROM_PROGRESS_WRITE)
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	else if (stage == FLASHROM_PROGRESS_ERASE)
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_ERASE);

	/* progress */
	fu_progress_set_percentage_full(progress, current, total);
}
#endif

static GBytes *
fu_flashrom_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(device);
	gint rc;
	gsize bufsz = fu_device_get_firmware_size_max(device);
	g_autofree guint8 *buf = g_malloc0(bufsz);

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
#ifdef HAVE_FLASHROM_SET_PROGRESS_CALLBACK_V2
	flashrom_set_progress_callback_v2(self->flashctx, fu_flashrom_device_progress_cb, progress);
#endif
	rc = flashrom_image_read(self->flashctx, buf, bufsz);
#ifdef HAVE_FLASHROM_SET_PROGRESS_CALLBACK_V2
	flashrom_set_progress_callback_v2(self->flashctx, NULL, NULL);
#endif
	if (rc != 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_READ, "failed to read flash [%i]", rc);
		return NULL;
	}
	return g_bytes_new_take(g_steal_pointer(&buf), bufsz);
}

static gboolean
fu_flashrom_device_prepare(FuDevice *device,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	gboolean exists_orig = FALSE;
	g_autofree gchar *firmware_orig = NULL;
	g_autofree gchar *localstatedir = NULL;
	g_autofree gchar *basename = NULL;

	/* if the original firmware doesn't exist, grab it now */
	basename = g_strdup_printf("flashrom-%s.bin", fu_device_get_id(device));
	localstatedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	firmware_orig = g_build_filename(localstatedir, "builder", basename, NULL);
	if (!fu_path_mkdir_parent(firmware_orig, error))
		return FALSE;
	if (!fu_device_query_file_exists(device, firmware_orig, &exists_orig, error))
		return FALSE;
	if (!exists_orig) {
		g_autoptr(GBytes) buf = NULL;
		buf = fu_flashrom_device_dump_firmware(device, progress, error);
		if (buf == NULL) {
			g_prefix_error(error, "failed to back up original firmware: ");
			return FALSE;
		}
		if (!fu_bytes_set_contents(firmware_orig, buf, error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
fu_flashrom_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(device);
	gsize sz = 0;
	gint rc;
	const guint8 *buf;
	g_autoptr(GBytes) blob_fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 10, NULL);

	/* read early */
	blob_fw = fu_firmware_get_bytes(firmware, error);
	if (blob_fw == NULL)
		return FALSE;
	buf = g_bytes_get_data(blob_fw, &sz);

	/* write region */
	if (sz != fu_device_get_firmware_size_max(device)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid image size 0x%x, expected 0x%x",
			    (guint)sz,
			    (guint)fu_device_get_firmware_size_max(device));
		return FALSE;
	}
#ifdef HAVE_FLASHROM_SET_PROGRESS_CALLBACK_V2
	flashrom_set_progress_callback_v2(self->flashctx, fu_flashrom_device_progress_cb, progress);
#endif
	rc = flashrom_image_write(self->flashctx, (void *)buf, sz, NULL /* refbuffer */);
#ifdef HAVE_FLASHROM_SET_PROGRESS_CALLBACK_V2
	flashrom_set_progress_callback_v2(self->flashctx, NULL, NULL);
#endif
	if (rc != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "image write failed, err=%i",
			    rc);
		return FALSE;
	}
	fu_progress_step_done(progress);

	if (flashrom_image_verify(self->flashctx, (void *)buf, sz)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "image verify failed");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* Check if CMOS needs a reset */
	if (fu_device_has_private_flag(device, FU_FLASHROM_DEVICE_FLAG_RESET_CMOS)) {
		g_debug("attempting CMOS reset");
		if (!fu_flashrom_cmos_reset(error)) {
			g_prefix_error(error, "failed CMOS reset: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_flashrom_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_flashrom_device_init(FuFlashromDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_protocol(FU_DEVICE(self), "org.flashrom");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ENSURE_SEMVER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_SIGNED);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE);
	fu_device_set_physical_id(FU_DEVICE(self), "flashrom");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_COMPUTER);
	fu_device_register_private_flag(FU_DEVICE(self), FU_FLASHROM_DEVICE_FLAG_RESET_CMOS);
	fu_device_register_private_flag(FU_DEVICE(self), FU_FLASHROM_DEVICE_FLAG_FN_M_ME_UNLOCK);
}

static void
fu_flashrom_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(object);
	switch (prop_id) {
	case PROP_FLASHCTX:
		g_value_set_pointer(value, self->flashctx);
		break;
	case PROP_REGION:
		g_value_set_uint(value, self->ifd_region);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_flashrom_device_set_property(GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(object);
	switch (prop_id) {
	case PROP_FLASHCTX:
		self->flashctx = g_value_get_pointer(value);
		break;
	case PROP_REGION:
		self->ifd_region = g_value_get_uint(value);
		fu_device_set_logical_id(FU_DEVICE(self),
					 fu_ifd_region_to_string(self->ifd_region));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_flashrom_device_finalize(GObject *object)
{
	FuFlashromDevice *self = FU_FLASHROM_DEVICE(object);
	if (self->layout != NULL)
		flashrom_layout_release(self->layout);
	g_free(self->fmap_regions);

	G_OBJECT_CLASS(fu_flashrom_device_parent_class)->finalize(object);
}

static void
fu_flashrom_device_class_init(FuFlashromDeviceClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	object_class->get_property = fu_flashrom_device_get_property;
	object_class->set_property = fu_flashrom_device_set_property;

	/**
	 * FuFlashromDevice:region:
	 *
	 * The IFD region that's being managed.
	 */
	pspec = g_param_spec_uint("region",
				  NULL,
				  NULL,
				  0,
				  G_MAXUINT,
				  0,
				  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_REGION, pspec);

	/**
	 * FuFlashromDevice:flashctx:
	 *
	 * The JSON root member for the device.
	 */
	pspec =
	    g_param_spec_pointer("flashctx",
				 NULL,
				 NULL,
				 G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FLASHCTX, pspec);

	object_class->finalize = fu_flashrom_device_finalize;
	device_class->to_string = fu_flashrom_device_to_string;
	device_class->set_quirk_kv = fu_flashrom_device_set_quirk_kv;
	device_class->probe = fu_flashrom_device_probe;
	device_class->open = fu_flashrom_device_open;
	device_class->close = fu_flashrom_device_close;
	device_class->set_progress = fu_flashrom_device_set_progress;
	device_class->prepare = fu_flashrom_device_prepare;
	device_class->dump_firmware = fu_flashrom_device_dump_firmware;
	device_class->write_firmware = fu_flashrom_device_write_firmware;
}

FuDevice *
fu_flashrom_device_new(FuContext *ctx, struct flashrom_flashctx *flashctx, FuIfdRegion ifd_region)
{
	return FU_DEVICE(g_object_new(FU_TYPE_FLASHROM_DEVICE,
				      "context",
				      ctx,
				      "flashctx",
				      flashctx,
				      "region",
				      ifd_region,
				      NULL));
}

gboolean
fu_flashrom_device_unlock(FuFlashromDevice *self, GError **error)
{
	if (self->ifd_region == FU_IFD_REGION_ME &&
	    fu_device_has_private_flag(FU_DEVICE(self), FU_FLASHROM_DEVICE_FLAG_FN_M_ME_UNLOCK)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "\n"
				    "ME region should be unlocked manually the following way:\n"
				    " 1. Power off your device\n"
				    " 2. Press and keep holding Fn + M during the next step\n"
				    " 3. Press power on button");
		return FALSE;
	}

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "Unlocking of device %s is not supported",
		    fu_device_get_name(FU_DEVICE(self)));
	return FALSE;
}
