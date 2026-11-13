/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-pixart-tp-device.h"
#include "fu-pixart-tp-firmware.h"
#include "fu-pixart-tp-ndt-haptic-device.h"
#include "fu-pixart-tp-ndt-struct.h"
#include "fu-pixart-tp-struct.h"

static void
fu_pixart_tp_firmware_json_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "pixart-tp.builder.json", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "43c45f8edde6f9df99abf67455b7c47fa03fc604",
						  FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

/* build the FWHD file from the builder XML, optionally replacing the ndt-force payload */
static GBytes *
fu_pixart_tp_ndt_build_fwhd(GBytes *ndt_blob)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *xml = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "pixart-tp.builder.json", NULL);
	ret = g_file_get_contents(filename, &xml, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	firmware = fu_firmware_new_from_json(xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	if (ndt_blob != NULL) {
		g_autoptr(FuFirmware) img =
		    fu_firmware_get_image_by_id(firmware, "ndt-force", &error);
		g_assert_no_error(error);
		g_assert_nonnull(img);
		fu_firmware_set_bytes(img, ndt_blob);
	}
	blob = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	return g_steal_pointer(&blob);
}

static FuFirmware *
fu_pixart_tp_ndt_prepare_firmware(GBytes *fwhd, GError **error)
{
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) proxy = g_object_new(FU_TYPE_PIXART_TP_DEVICE, "context", ctx, NULL);
	g_autoptr(FuPixartTpNdtHapticDevice) device = fu_pixart_tp_ndt_haptic_device_new(proxy);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(fwhd));

	return fu_device_prepare_firmware(FU_DEVICE(device),
					  stream,
					  progress,
					  FU_FIRMWARE_PARSE_FLAG_NONE,
					  error);
}

static void
fu_pixart_tp_ndt_prepare_firmware_func(void)
{
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) fwhd = fu_pixart_tp_ndt_build_fwhd(NULL);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	img = fu_pixart_tp_ndt_prepare_firmware(fwhd, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img);
	g_assert_cmpint(fu_firmware_get_idx(img), ==, FU_PIXART_TP_UPDATE_TYPE_NDT_FORCE);
	blob = fu_firmware_get_bytes(img, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	g_assert_cmpint(g_bytes_get_size(blob), ==, FU_STRUCT_PIXART_TP_NDT_IMAGE_HEADER_SIZE + 12);
}

/* a valid header with an optional u32 overwritten, to break the struct constants */
static GBytes *
fu_pixart_tp_ndt_image_new(guint32 valid_data_len,
			   gsize payload_sz,
			   gsize bad_offset,
			   guint32 bad_value)
{
	gboolean ret;
	g_autoptr(FuStructPixartTpNdtImageHeader) st = fu_struct_pixart_tp_ndt_image_header_new();
	g_autoptr(GError) error = NULL;

	fu_struct_pixart_tp_ndt_image_header_set_sw_version(st, 0x2C5A0300);
	fu_struct_pixart_tp_ndt_image_header_set_valid_data_len(st, valid_data_len);
	if (bad_offset != G_MAXSIZE) {
		ret = fu_memwrite_uint32_safe(st->buf->data,
					      st->buf->len,
					      bad_offset,
					      bad_value,
					      G_LITTLE_ENDIAN,
					      &error);
		g_assert_no_error(error);
		g_assert_true(ret);
	}
	fu_byte_array_set_size(st->buf, st->buf->len + payload_sz, 0xAA);
	return g_bytes_new(st->buf->data, st->buf->len);
}

static void
fu_pixart_tp_ndt_prepare_firmware_invalid_func(void)
{
	struct {
		const gchar *name;
		guint32 valid_data_len;
		gsize payload_sz;
		gsize bad_offset;
		guint32 bad_value;
		FwupdError error_code;
	} cases[] = {
	    {"bad para_data_type",
	     12,
	     12,
	     FU_STRUCT_PIXART_TP_NDT_IMAGE_HEADER_OFFSET_PARA_DATA_TYPE,
	     0x22,
	     FWUPD_ERROR_INVALID_DATA},
	    {"bad download_addr",
	     12,
	     12,
	     FU_STRUCT_PIXART_TP_NDT_IMAGE_HEADER_OFFSET_DOWNLOAD_ADDR,
	     0x01002000,
	     FWUPD_ERROR_INVALID_DATA},
	    {"zero valid_data_len", 0, 0, G_MAXSIZE, 0, FWUPD_ERROR_INVALID_FILE},
	    {"size mismatch", 12, 11, G_MAXSIZE, 0, FWUPD_ERROR_INVALID_FILE},
	};

	/* sanity check the helper itself */
	{
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GBytes) ndt = fu_pixart_tp_ndt_image_new(12, 12, G_MAXSIZE, 0);
		g_autoptr(GBytes) fwhd = fu_pixart_tp_ndt_build_fwhd(ndt);
		g_autoptr(GError) error = NULL;

		img = fu_pixart_tp_ndt_prepare_firmware(fwhd, &error);
		g_assert_no_error(error);
		g_assert_nonnull(img);
	}

	for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GBytes) fwhd = NULL;
		g_autoptr(GBytes) ndt = NULL;
		g_autoptr(GError) error = NULL;

		g_debug("testing %s", cases[i].name);
		ndt = fu_pixart_tp_ndt_image_new(cases[i].valid_data_len,
						 cases[i].payload_sz,
						 cases[i].bad_offset,
						 cases[i].bad_value);
		fwhd = fu_pixart_tp_ndt_build_fwhd(ndt);
		img = fu_pixart_tp_ndt_prepare_firmware(fwhd, &error);
		g_assert_error(error, FWUPD_ERROR, (gint)cases[i].error_code);
		g_assert_null(img);
	}

	/* truncated header */
	{
		const guint8 buf[0x10] = {0};
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GBytes) ndt = g_bytes_new_static(buf, sizeof(buf));
		g_autoptr(GBytes) fwhd = fu_pixart_tp_ndt_build_fwhd(ndt);
		g_autoptr(GError) error = NULL;

		img = fu_pixart_tp_ndt_prepare_firmware(fwhd, &error);
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_READ);
		g_assert_null(img);
	}
}

static FuFirmware *
fu_pixart_tp_test_prepare_firmware(GBytes *blob, FuDevice *device, GError **error)
{
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(blob));
	return fu_device_prepare_firmware(device,
					  stream,
					  progress,
					  FU_FIRMWARE_PARSE_FLAG_NONE,
					  error);
}

/* the builder XML has both haptic sections, but a real TP only has one haptic solution */
static GBytes *
fu_pixart_tp_test_build_blob(const gchar *id_remove)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "pixart-tp.builder.json", NULL);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	if (id_remove != NULL) {
		ret = fu_firmware_remove_image_by_id(firmware, id_remove, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
	}
	blob = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	return g_steal_pointer(&blob);
}

static void
fu_pixart_tp_test_check_firmware_child(const gchar *flag, const gchar *id_other)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_PIXART_TP_DEVICE, "context", ctx, NULL);
	g_autoptr(FuFirmware) firmware_bad = NULL;
	g_autoptr(FuFirmware) firmware_ok = NULL;
	g_autoptr(FuFirmware) firmware_other = NULL;
	g_autoptr(GBytes) blob = fu_pixart_tp_test_build_blob(id_other);
	g_autoptr(GBytes) blob_both = fu_pixart_tp_test_build_blob(NULL);
	g_autoptr(GError) error = NULL;

	/* no child device writes the haptic section, so fail before anything is erased */
	firmware_bad = fu_pixart_tp_test_prepare_firmware(blob, device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(firmware_bad);
	g_clear_error(&error);

	/* the haptic child device writes its own section */
	/* the child device ID is built from this, but nothing is ever opened */
	fu_device_set_physical_id(device, "test");
	fu_device_add_private_flag(device, flag);
	ret = fu_device_probe(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	firmware_ok = fu_pixart_tp_test_prepare_firmware(blob, device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware_ok);

	/* nothing writes the section for the other haptic solution */
	firmware_other = fu_pixart_tp_test_prepare_firmware(blob_both, device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(firmware_other);
}

static void
fu_pixart_tp_device_check_firmware_func(void)
{
	fu_pixart_tp_test_check_firmware_child("has-haptic", "ndt-force");
	fu_pixart_tp_test_check_firmware_child("has-ndt-haptic", "tf-force");
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_PIXART_TP_FIRMWARE);
	g_test_add_func("/pixart-tp/firmware/json", fu_pixart_tp_firmware_json_func);
	g_test_add_func("/pixart-tp/device/check-firmware",
			fu_pixart_tp_device_check_firmware_func);
	g_test_add_func("/pixart-tp/ndt/prepare-firmware", fu_pixart_tp_ndt_prepare_firmware_func);
	g_test_add_func("/pixart-tp/ndt/prepare-firmware{invalid}",
			fu_pixart_tp_ndt_prepare_firmware_invalid_func);
	return g_test_run();
}
