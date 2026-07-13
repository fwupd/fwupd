/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
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

#include <glib/gstdio.h>

#include "fu-config-private.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-mtd-device.h"
#include "fu-mtd-ifd-device.h"
#include "fu-security-attrs-private.h"
#include "fu-udev-device-private.h"

typedef struct {
	FuContext *ctx;
} FuTest;

#define FU_TEST_MTD_DEVICE_SIZE 0x100000

static void
fu_test_free(FuTest *self)
{
	if (self->ctx != NULL)
		g_object_unref(self->ctx);
	g_free(self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuTest, fu_test_free)

static FuMtdDevice *
fu_test_mtd_find_mtdram(FuContext *ctx, GError **error)
{
	g_autoptr(GPtrArray) mtd_files = NULL;
	g_autoptr(FuDevice) device = NULL;
	const gchar *device_file;

	mtd_files = fu_path_glob("/sys/devices/virtual/mtd", "mtd?", error);
	if (mtd_files == NULL) {
		g_prefix_error_literal(error, "no mtdram device: ");
		return NULL;
	}

	/* create device */
	device_file = g_ptr_array_index(mtd_files, 0);
	device = g_object_new(FU_TYPE_MTD_DEVICE, "context", ctx, "backend-id", device_file, NULL);
	if (!fu_device_probe(device, error))
		return NULL;
	if (g_strcmp0(fu_device_get_name(device), "mtdram test device") != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "device is not mtdram test device");
		return NULL;
	}
	fu_device_set_firmware_gtype(FU_DEVICE(device), G_TYPE_INVALID);
	if (!fu_device_open(device, error))
		return NULL;
	return FU_MTD_DEVICE(g_steal_pointer(&device));
}

#ifdef HAVE_MTD_USER_H
static void
fu_test_mtd_device_add_memislocked_event(FuMtdDevice *device, gboolean locked)
{
	struct erase_info_user erase = {0x0};
	g_autofree gchar *data = NULL;
	g_autofree gchar *event_id = NULL;
	g_autoptr(FuDeviceEvent) event = NULL;

	erase.start = 0x0;
	erase.length = FU_TEST_MTD_DEVICE_SIZE;
	data = fu_base64_encode((const guint8 *)&erase, sizeof(erase));
	event_id = g_strdup_printf("Ioctl:Request=0x%04x,Data=%s,Length=0x%x",
				   (guint)MEMISLOCKED,
				   data,
				   (guint)sizeof(erase));
	event = fu_device_event_new(event_id);
	fu_device_event_set_data(event, "DataOut", (const guint8 *)&erase, sizeof(erase));
	fu_device_event_set_i64(event, "Rc", locked ? 1 : 0);
	fu_device_add_event(FU_DEVICE(device), event);
}

static void
fu_test_mtd_device_write_sysfs_attr(const gchar *sysfs_path, const gchar *attr, const gchar *value)
{
	g_autofree gchar *fn = g_build_filename(sysfs_path, attr, NULL);
	g_autoptr(GError) error = NULL;

	g_assert_true(g_file_set_contents(fn, value, -1, &error));
	g_assert_no_error(error);
}

static FuMtdDevice *
fu_test_mtd_device_new_for_security_attrs(FuTest *self, gboolean add_event, gboolean locked)
{
	gboolean ret;
	g_autofree gchar *sysfs_path = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("mtd-security-attrs", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	sysfs_path =
	    fu_temporary_directory_build(tmpdir, "sys", "devices", "virtual", "mtd", "mtd0", NULL);
	g_assert_cmpint(g_mkdir_with_parents(sysfs_path, 0700), ==, 0);
	fu_test_mtd_device_write_sysfs_attr(sysfs_path, "flags", "0x400");
	fu_test_mtd_device_write_sysfs_attr(sysfs_path, "name", "BIOS");
	fu_test_mtd_device_write_sysfs_attr(sysfs_path, "type", "nor");
	fu_test_mtd_device_write_sysfs_attr(sysfs_path, "size", "0x100000");
	fu_test_mtd_device_write_sysfs_attr(sysfs_path, "erasesize", "0x1000");

	device =
	    g_object_new(FU_TYPE_MTD_DEVICE, "context", self->ctx, "backend-id", sysfs_path, NULL);
	fu_device_set_plugin(device, "mtd");
	fu_udev_device_set_subsystem(FU_UDEV_DEVICE(device), "mtd");
	fu_udev_device_set_devtype(FU_UDEV_DEVICE(device), "mtd");
	ret = fu_device_probe(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);
	if (add_event)
		fu_test_mtd_device_add_memislocked_event(FU_MTD_DEVICE(device), locked);
	return FU_MTD_DEVICE(g_steal_pointer(&device));
}
#endif

static FuFirmware *
fu_test_mtd_prepare_mtdram_device(FuMtdDevice *device,
				  GType firmware_gtype,
				  const gchar *filename_xml)
{
	gboolean ret;
	gsize bufsz = fu_device_get_firmware_size_max(FU_DEVICE(device));
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* build the image */
	if (filename_xml != NULL) {
		g_autoptr(GBytes) blob_tmp = NULL;
		g_autofree gchar *filename = NULL;

		filename = g_test_build_filename(G_TEST_DIST, "tests", filename_xml, NULL);
		g_debug("loading from %s", filename);
		firmware = fu_firmware_new_from_filename(filename, &error);
		g_assert_no_error(error);
		g_assert_nonnull(firmware);
		blob_tmp = fu_firmware_write(firmware, &error);
		g_assert_no_error(error);
		g_assert_nonnull(blob_tmp);
		blob = fu_bytes_pad(blob_tmp, bufsz, 0xFF);
	} else {
		g_autoptr(GByteArray) buf = g_byte_array_new();
		firmware = fu_firmware_new();
		fu_byte_array_set_size(buf, bufsz, 0xFF);
		blob = g_bytes_new(buf->data, buf->len);
	}
	fu_firmware_set_bytes(firmware, blob);
	ret = fu_device_write_firmware(FU_DEVICE(device),
				       firmware,
				       progress,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* success */
	fu_device_probe_invalidate(FU_DEVICE(device));
	fu_device_set_firmware_gtype(FU_DEVICE(device), firmware_gtype);
	return g_steal_pointer(&firmware);
}

static void
fu_test_mtd_ifd_device_probe_bios_rw_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) proxy = g_object_new(FU_TYPE_MTD_DEVICE, "context", self->ctx, NULL);
	g_autoptr(FuIfdImage) img = FU_IFD_IMAGE(fu_ifd_image_new());
	g_autoptr(FuMtdIfdDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	fu_firmware_set_idx(FU_FIRMWARE(img), FU_IFD_REGION_BIOS);
	fu_ifd_image_set_access(img, FU_IFD_REGION_BIOS, FU_IFD_ACCESS_READ | FU_IFD_ACCESS_WRITE);
	device = fu_mtd_ifd_device_new(proxy, img);
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_name(FU_DEVICE(device)), ==, "BIOS");
	g_assert_cmpstr(fu_device_get_logical_id(FU_DEVICE(device)), ==, "bios");
	g_assert_true(fu_device_has_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE));
	g_assert_true(fu_device_has_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UPDATABLE));
}

static void
fu_test_mtd_ifd_device_probe_bios_ro_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) proxy = g_object_new(FU_TYPE_MTD_DEVICE, "context", self->ctx, NULL);
	g_autoptr(FuIfdImage) img = FU_IFD_IMAGE(fu_ifd_image_new());
	g_autoptr(FuMtdIfdDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	fu_firmware_set_idx(FU_FIRMWARE(img), FU_IFD_REGION_BIOS);
	fu_ifd_image_set_access(img, FU_IFD_REGION_BIOS, FU_IFD_ACCESS_READ);
	device = fu_mtd_ifd_device_new(proxy, img);
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_name(FU_DEVICE(device)), ==, "BIOS");
	g_assert_true(fu_device_has_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE));
	g_assert_false(fu_device_has_flag(FU_DEVICE(device), FWUPD_DEVICE_FLAG_UPDATABLE));
}

static void
fu_test_mtd_ifd_device_security_attrs_locked_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(FuDevice) proxy = g_object_new(FU_TYPE_MTD_DEVICE, "context", self->ctx, NULL);
	g_autoptr(FuIfdImage) img = FU_IFD_IMAGE(fu_ifd_image_new());
	g_autoptr(FuMtdIfdDevice) device = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GError) error = NULL;

	fu_firmware_set_idx(FU_FIRMWARE(img), FU_IFD_REGION_DESC);
	fu_ifd_image_set_access(img, FU_IFD_REGION_BIOS, FU_IFD_ACCESS_READ);
	fu_ifd_image_set_access(img, FU_IFD_REGION_ME, FU_IFD_ACCESS_NONE);
	fu_ifd_image_set_access(img, FU_IFD_REGION_EC, FU_IFD_ACCESS_NONE);
	device = fu_mtd_ifd_device_new(proxy, img);
	fu_device_set_plugin(FU_DEVICE(device), "mtd");
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_add_security_attrs(FU_DEVICE(device), attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_test_mtd_ifd_device_security_attrs_writable_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(FuDevice) proxy = g_object_new(FU_TYPE_MTD_DEVICE, "context", self->ctx, NULL);
	g_autoptr(FuIfdImage) img = FU_IFD_IMAGE(fu_ifd_image_new());
	g_autoptr(FuMtdIfdDevice) device = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GError) error = NULL;

	fu_firmware_set_idx(FU_FIRMWARE(img), FU_IFD_REGION_DESC);
	fu_ifd_image_set_access(img, FU_IFD_REGION_BIOS, FU_IFD_ACCESS_READ | FU_IFD_ACCESS_WRITE);
	fu_ifd_image_set_access(img, FU_IFD_REGION_ME, FU_IFD_ACCESS_NONE);
	fu_ifd_image_set_access(img, FU_IFD_REGION_EC, FU_IFD_ACCESS_NONE);
	device = fu_mtd_ifd_device_new(proxy, img);
	fu_device_set_plugin(FU_DEVICE(device), "mtd");
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_add_security_attrs(FU_DEVICE(device), attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR,
						     NULL);
	g_assert_nonnull(attr);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
}

static void
fu_test_mtd_ifd_device_security_attrs_non_desc_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(FuDevice) proxy = g_object_new(FU_TYPE_MTD_DEVICE, "context", self->ctx, NULL);
	g_autoptr(FuIfdImage) img = FU_IFD_IMAGE(fu_ifd_image_new());
	g_autoptr(FuMtdIfdDevice) device = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(GError) error = NULL;

	fu_firmware_set_idx(FU_FIRMWARE(img), FU_IFD_REGION_BIOS);
	device = fu_mtd_ifd_device_new(proxy, img);
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_add_security_attrs(FU_DEVICE(device), attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR,
						     NULL);
	g_assert_null(attr);
}

static void
fu_test_mtd_device_security_attrs_locked_func(gconstpointer user_data)
{
#ifndef HAVE_MTD_USER_H
	g_test_skip("no mtd-user.h support");
#else
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(FuMtdDevice) device = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();

	device = fu_test_mtd_device_new_for_security_attrs(self, TRUE, TRUE);
	fu_device_add_security_attrs(FU_DEVICE(device), attrs);

	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_MTD_LOCKED, NULL);
	g_assert_nonnull(attr);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
	g_assert_cmpint(fwupd_security_attr_get_result_success(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_LOCKED);
#endif
}

static void
fu_test_mtd_device_security_attrs_unlocked_func(gconstpointer user_data)
{
#ifndef HAVE_MTD_USER_H
	g_test_skip("no mtd-user.h support");
#else
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(FuMtdDevice) device = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();

	device = fu_test_mtd_device_new_for_security_attrs(self, TRUE, FALSE);
	fu_device_add_security_attrs(FU_DEVICE(device), attrs);

	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_MTD_LOCKED, NULL);
	g_assert_nonnull(attr);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
	g_assert_true(
	    fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM));
#endif
}

static void
fu_test_mtd_device_security_attrs_missing_func(gconstpointer user_data)
{
#ifndef HAVE_MTD_USER_H
	g_test_skip("no mtd-user.h support");
#else
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(FuMtdDevice) device = NULL;
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();

	device = fu_test_mtd_device_new_for_security_attrs(self, FALSE, FALSE);
	fu_device_add_security_attrs(FU_DEVICE(device), attrs);

	attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_MTD_LOCKED, NULL);
	g_assert_nonnull(attr);
	g_assert_false(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA));
#endif
}

static void
fu_test_mtd_device_quirk_metadata_offset_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_MTD_DEVICE, "context", self->ctx, NULL);
	g_autoptr(GError) error = NULL;

	ret = fu_device_set_quirk_kv(device,
				     "MtdMetadataOffset",
				     "0x1000",
				     FU_CONTEXT_QUIRK_SOURCE_DB,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_test_mtd_device_quirk_metadata_size_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_MTD_DEVICE, "context", self->ctx, NULL);
	g_autoptr(GError) error = NULL;

	ret = fu_device_set_quirk_kv(device,
				     "MtdMetadataSize",
				     "0x200",
				     FU_CONTEXT_QUIRK_SOURCE_DB,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_test_mtd_device_quirk_fmap_offset_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_MTD_DEVICE, "context", self->ctx, NULL);
	g_autoptr(GError) error = NULL;

	ret = fu_device_set_quirk_kv(device,
				     "MtdFmapOffset",
				     "0x500",
				     FU_CONTEXT_QUIRK_SOURCE_DB,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_device_get_firmware_gtype(device), ==, FU_TYPE_FMAP_FIRMWARE);
}

static void
fu_test_mtd_device_quirk_fmap_regions_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_MTD_DEVICE, "context", self->ctx, NULL);
	g_autoptr(GError) error = NULL;

	ret = fu_device_set_quirk_kv(device,
				     "MtdFmapRegions",
				     "SBOM,FMAP",
				     FU_CONTEXT_QUIRK_SOURCE_DB,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_test_mtd_device_quirk_fmap_regions_empty_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_MTD_DEVICE, "context", self->ctx, NULL);
	g_autoptr(GError) error = NULL;

	ret = fu_device_set_quirk_kv(device,
				     "MtdFmapRegions",
				     ",",
				     FU_CONTEXT_QUIRK_SOURCE_DB,
				     &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

static void
fu_test_mtd_device_quirk_unknown_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_MTD_DEVICE, "context", self->ctx, NULL);
	g_autoptr(GError) error = NULL;

	ret = fu_device_set_quirk_kv(device,
				     "InvalidKey",
				     "value",
				     FU_CONTEXT_QUIRK_SOURCE_DB,
				     &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

static void
fu_test_mtd_device_raw_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuMtdDevice) device = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(GBytes) fw2 = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;

	/* find correct device */
	device = fu_test_mtd_find_mtdram(self->ctx, &error);
	if (device == NULL) {
		g_test_skip(error->message);
		return;
	}

	/* write the IFD image */
	firmware = fu_test_mtd_prepare_mtdram_device(device, FU_TYPE_FIRMWARE, NULL);
	g_assert_nonnull(firmware);
	fw = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fw);

	/* dump back */
	fu_progress_reset(progress);
	fw2 = fu_device_dump_firmware(FU_DEVICE(device), progress, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fw2);

	/* verify */
	ret = fu_bytes_compare(fw, fw2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_test_mtd_device_read_firmware_invalid_gtype_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	g_autoptr(FuMtdDevice) device = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(GError) error = NULL;

	/* find correct device; this leaves the firmware gtype as G_TYPE_INVALID */
	device = fu_test_mtd_find_mtdram(self->ctx, &error);
	if (device == NULL) {
		g_test_skip(error->message);
		return;
	}
	g_assert_cmpint(fu_device_get_firmware_gtype(FU_DEVICE(device)), ==, G_TYPE_INVALID);

	/* with no specific firmware type set we should fall back to a generic
	 * FuFirmware rather than crashing on g_object_new(G_TYPE_INVALID) */
	firmware = fu_device_read_firmware(FU_DEVICE(device),
					   progress,
					   FU_FIRMWARE_PARSE_FLAG_NONE,
					   &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
}

static void
fu_test_mtd_device_ifd_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuMtdDevice) device = NULL;
	g_autoptr(FuDevice) device_bios = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) firmware_bios = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(GError) error = NULL;

	/* find correct device */
	device = fu_test_mtd_find_mtdram(self->ctx, &error);
	if (device == NULL) {
		g_test_skip(error->message);
		return;
	}

	/* write the IFD image */
	firmware =
	    fu_test_mtd_prepare_mtdram_device(device, FU_TYPE_IFD_FIRMWARE, "mtd-ifd.builder.xml");
	g_assert_nonnull(firmware);

	/* re-probe image */
	ret = fu_device_setup(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	device_bios = fu_device_get_child_by_logical_id(FU_DEVICE(device), "bios", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device_bios);

	/* just for debugging */
	str = fu_device_to_string(FU_DEVICE(device));
	g_debug("%s", str);

	/* re-write just the BIOS */
	firmware_bios = fu_firmware_get_image_by_id(firmware, "bios", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device_bios);
	ret = fu_device_write_firmware(device_bios,
				       firmware_bios,
				       progress,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_test_mtd_device_fmap_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuMtdDevice) device = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(GError) error = NULL;

	/* find correct device */
	device = fu_test_mtd_find_mtdram(self->ctx, &error);
	if (device == NULL) {
		g_test_skip(error->message);
		return;
	}

	/* write the FMAP image */
	firmware = fu_test_mtd_prepare_mtdram_device(device,
						     FU_TYPE_FMAP_FIRMWARE,
						     "mtd-fmap.builder.xml");
	g_assert_nonnull(firmware);

	/* re-probe image */
	ret = fu_device_setup(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_version(device), ==, "456");

	/* re-write firmware, this time in chunks */
	ret = fu_device_set_quirk_kv(FU_DEVICE(device),
				     "MtdFmapRegions",
				     "SBOM,FMAP",
				     FU_CONTEXT_QUIRK_SOURCE_DB,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_write_firmware(FU_DEVICE(device),
				       firmware,
				       progress,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_test_mtd_device_uswid_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuMtdDevice) device = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GError) error = NULL;

	/* find correct device */
	device = fu_test_mtd_find_mtdram(self->ctx, &error);
	if (device == NULL) {
		g_test_skip(error->message);
		return;
	}

	/* write the uSWID image */
	firmware = fu_test_mtd_prepare_mtdram_device(device,
						     FU_TYPE_USWID_FIRMWARE,
						     "mtd-uswid.builder.xml");
	g_assert_nonnull(firmware);

	/* re-probe image */
	ret = fu_device_setup(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_version(device), ==, "456");
}

static void
fu_test_mtd_device_smbios_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuMtdDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	/* find correct device */
	device = fu_test_mtd_find_mtdram(self->ctx, &error);
	if (device == NULL) {
		g_test_skip(error->message);
		return;
	}

	/* write the empty image */
	firmware = fu_test_mtd_prepare_mtdram_device(device, FU_TYPE_FIRMWARE, NULL);
	g_assert_nonnull(firmware);

	/* re-probe image */
	fu_device_add_private_flag(FU_DEVICE(device), FU_MTD_DEVICE_FLAG_SMBIOS_VERSION_FALLBACK);
	ret = fu_device_setup(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_version(FU_DEVICE(device)), ==, "1.59");
	g_assert_cmpint(fu_device_get_version_format(FU_DEVICE(device)),
			==,
			FWUPD_VERSION_FORMAT_PAIR);
}

int
main(int argc, char **argv)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuTest) self = g_new0(FuTest, 1);

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);

	/* do not save silo */
	self->ctx = fu_context_new();
	fu_context_set_path(self->ctx, FU_PATH_KIND_SYSCONFDIR_PKG, testdatadir);
	fu_config_set_basename(fu_context_get_config(self->ctx), "mtd-fwupd.conf");
	fu_context_add_flag(self->ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(self->ctx, progress, FU_CONTEXT_LOAD_FLAG_HWID_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_test_add_data_func("/mtd/ifd-device/probe/bios-rw",
			     self,
			     fu_test_mtd_ifd_device_probe_bios_rw_func);
	g_test_add_data_func("/mtd/ifd-device/probe/bios-ro",
			     self,
			     fu_test_mtd_ifd_device_probe_bios_ro_func);
	g_test_add_data_func("/mtd/ifd-device/security-attrs/locked",
			     self,
			     fu_test_mtd_ifd_device_security_attrs_locked_func);
	g_test_add_data_func("/mtd/ifd-device/security-attrs/writable",
			     self,
			     fu_test_mtd_ifd_device_security_attrs_writable_func);
	g_test_add_data_func("/mtd/ifd-device/security-attrs/non-desc",
			     self,
			     fu_test_mtd_ifd_device_security_attrs_non_desc_func);
	g_test_add_data_func("/mtd/device/security-attrs/locked",
			     self,
			     fu_test_mtd_device_security_attrs_locked_func);
	g_test_add_data_func("/mtd/device/security-attrs/unlocked",
			     self,
			     fu_test_mtd_device_security_attrs_unlocked_func);
	g_test_add_data_func("/mtd/device/security-attrs/missing",
			     self,
			     fu_test_mtd_device_security_attrs_missing_func);
	g_test_add_data_func("/mtd/device/quirk/metadata-offset",
			     self,
			     fu_test_mtd_device_quirk_metadata_offset_func);
	g_test_add_data_func("/mtd/device/quirk/metadata-size",
			     self,
			     fu_test_mtd_device_quirk_metadata_size_func);
	g_test_add_data_func("/mtd/device/quirk/fmap-offset",
			     self,
			     fu_test_mtd_device_quirk_fmap_offset_func);
	g_test_add_data_func("/mtd/device/quirk/fmap-regions",
			     self,
			     fu_test_mtd_device_quirk_fmap_regions_func);
	g_test_add_data_func("/mtd/device/quirk/fmap-regions-empty",
			     self,
			     fu_test_mtd_device_quirk_fmap_regions_empty_func);
	g_test_add_data_func("/mtd/device/quirk/unknown",
			     self,
			     fu_test_mtd_device_quirk_unknown_func);
	g_test_add_data_func("/mtd/device/raw", self, fu_test_mtd_device_raw_func);
	g_test_add_data_func("/mtd/device/read-firmware/invalid-gtype",
			     self,
			     fu_test_mtd_device_read_firmware_invalid_gtype_func);
	g_test_add_data_func("/mtd/device/uswid", self, fu_test_mtd_device_uswid_func);
	g_test_add_data_func("/mtd/device/ifd", self, fu_test_mtd_device_ifd_func);
	g_test_add_data_func("/mtd/device/fmap", self, fu_test_mtd_device_fmap_func);
	g_test_add_data_func("/mtd/device/smbios", self, fu_test_mtd_device_smbios_func);
	return g_test_run();
}
