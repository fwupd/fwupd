/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-pixart-tp-device.h"
#include "fu-pixart-tp-firmware.h"

static void
fu_pixart_tp_firmware_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "pixart-tp.builder.xml", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "a9ed17b970a867c190f62be59338dbad89d07553",
						  FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
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

static void
fu_pixart_tp_device_check_firmware_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_PIXART_TP_DEVICE, "context", ctx, NULL);
	g_autoptr(FuFirmware) firmware_src = NULL;
	g_autoptr(FuFirmware) firmware_bad = NULL;
	g_autoptr(FuFirmware) firmware_ok = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* a TP image with a TF_FORCE section */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "pixart-tp.builder.xml", NULL);
	firmware_src = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware_src);
	blob = fu_firmware_write(firmware_src, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	/* no child device writes TF_FORCE, so fail before anything is erased */
	firmware_bad = fu_pixart_tp_test_prepare_firmware(blob, device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(firmware_bad);
	g_clear_error(&error);

	/* the haptic child device writes TF_FORCE */
	/* the child device ID is built from this, but nothing is ever opened */
	fu_device_set_physical_id(device, "test");
	fu_device_add_private_flag(device, "has-haptic");
	ret = fu_device_probe(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	firmware_ok = fu_pixart_tp_test_prepare_firmware(blob, device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware_ok);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_PIXART_TP_FIRMWARE);
	g_test_add_func("/pixart-tp/firmware/xml", fu_pixart_tp_firmware_xml_func);
	g_test_add_func("/pixart-tp/device/check-firmware",
			fu_pixart_tp_device_check_firmware_func);
	return g_test_run();
}
