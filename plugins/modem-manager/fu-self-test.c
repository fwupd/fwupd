/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-mm-device.h"
#include "fu-mm-mhi-firehose-device.h"

static void
fu_mm_test_add_image(FuFirmware *firmware, const gchar *id, const gchar *data)
{
	g_autoptr(FuFirmware) image = fu_zip_file_new();
	g_autoptr(GError) error = NULL;

	fu_firmware_set_id(image, id);
	fu_firmware_set_bytes(image, g_bytes_new_static(data, strlen(data)));
	g_assert_true(fu_firmware_add_image(firmware, image, &error));
	g_assert_no_error(error);
}

static const gchar *fu_mm_test_mapping_xml =
    "<TableList>"
    "<WwanDeviceConfigureIDTable>"
    "<UniqueSkuIDToWwanDeviceConfigureIDMappingTable>"
    "<UniqueSkuID WwanDeviceConfigID=\"dev-a\" ProductID=\"MODEL-A\"/>"
    "<UniqueSkuID WwanDeviceConfigID=\"dev-a\" ProductID=\"MODEL-A\"/>"
    "<UniqueSkuID WwanDeviceConfigID=\"dev-b\" ProductID=\"MODEL-B\"/>"
    "</UniqueSkuIDToWwanDeviceConfigureIDMappingTable>"
    "</WwanDeviceConfigureIDTable>"
    "<WwanDeviceConfigureIDToImageMappingTable>"
    "<WwanDeviceConfigID ID=\"dev-a\">"
    "<Standard><DEVFirmware File=\"dev-a\"/></Standard>"
    "</WwanDeviceConfigID>"
    "<WwanDeviceConfigID ID=\"dev-b\">"
    "<Standard><DEVFirmware File=\"dev-b\"/></Standard>"
    "</WwanDeviceConfigID>"
    "</WwanDeviceConfigureIDToImageMappingTable>"
    "</TableList>";

static const gchar *fu_mm_test_ambiguous_mapping_xml =
    "<TableList>"
    "<WwanDeviceConfigureIDTable>"
    "<UniqueSkuIDToWwanDeviceConfigureIDMappingTable>"
    "<UniqueSkuID WwanDeviceConfigID=\"dev-a\" ProductID=\"MODEL-A\"/>"
    "<UniqueSkuID WwanDeviceConfigID=\"dev-b\" ProductID=\"MODEL-A\"/>"
    "</UniqueSkuIDToWwanDeviceConfigureIDMappingTable>"
    "</WwanDeviceConfigureIDTable>"
    "<WwanDeviceConfigureIDToImageMappingTable/>"
    "</TableList>";

static const gchar *fu_mm_test_rawprogram_xml =
    "<data>"
    "<program filename=\"boot.img\" label=\"boot\"/>"
    "<program filename=\"\" label=\"f_dev_pack\"/>"
    "</data>";

static void
fu_mm_mhi_firehose_dev_image_func(void)
{
	gboolean ret;
	gsize rawprogram_sz = 0;
	const gchar *product_ids[] = {"UNKNOWN", "MODEL-A", NULL};
	g_autofree gchar *rawprogram = NULL;
	g_autoptr(GBytes) dev_image = NULL;
	g_autoptr(GBytes) dev_image_expected = g_bytes_new_static("IMAGE-A", 7);
	g_autoptr(GBytes) rawprogram_blob = NULL;
	g_autoptr(FuFirmware) firmware = fu_zip_firmware_new();
	g_autoptr(GError) error = NULL;

	fu_mm_test_add_image(firmware,
			     "DEV_OTA_PACKAGE/WwanDeviceIdImageMappingTable.xml",
			     fu_mm_test_mapping_xml);
	fu_mm_test_add_image(firmware, "DEV_OTA_PACKAGE/dev-a/devicepack.ubi", "IMAGE-A");
	fu_mm_test_add_image(firmware, "DEV_OTA_PACKAGE/dev-b/devicepack.ubi", "IMAGE-B");
	fu_mm_test_add_image(firmware, "firehose-rawprogram.xml", fu_mm_test_rawprogram_xml);

	ret = fu_mm_mhi_firehose_prepare_dev_image(firmware, product_ids, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	dev_image = fu_firmware_get_image_by_id_bytes(firmware, "devicepack.ubi", &error);
	g_assert_no_error(error);
	g_assert_true(g_bytes_equal(dev_image, dev_image_expected));
	rawprogram_blob =
	    fu_firmware_get_image_by_id_bytes(firmware, "firehose-rawprogram.xml", &error);
	g_assert_no_error(error);
	rawprogram =
	    g_strndup(g_bytes_get_data(rawprogram_blob, &rawprogram_sz), rawprogram_sz);
	g_assert_nonnull(g_strstr_len(rawprogram, rawprogram_sz, "filename=\"devicepack.ubi\""));
}

static void
fu_mm_mhi_firehose_dev_image_ambiguous_func(void)
{
	const gchar *product_ids[] = {"MODEL-A", NULL};
	g_autoptr(FuFirmware) firmware = fu_zip_firmware_new();
	g_autoptr(GError) error = NULL;

	fu_mm_test_add_image(firmware,
			     "DEV_OTA_PACKAGE/WwanDeviceIdImageMappingTable.xml",
			     fu_mm_test_ambiguous_mapping_xml);
	g_assert_false(fu_mm_mhi_firehose_prepare_dev_image(firmware, product_ids, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
}

static void
fu_mm_mhi_firehose_dev_image_legacy_func(void)
{
	const gchar *product_ids[] = {"MODEL-A", NULL};
	g_autoptr(FuFirmware) firmware = fu_zip_firmware_new();
	g_autoptr(GError) error = NULL;

	fu_mm_test_add_image(firmware, "firehose-rawprogram.xml", fu_mm_test_rawprogram_xml);
	g_assert_true(
	    fu_mm_mhi_firehose_prepare_dev_image(firmware, product_ids, &error));
	g_assert_no_error(error);
	g_assert_null(fu_firmware_get_image_by_id(firmware, "devicepack.ubi", NULL));
}

static void
fu_mm_device_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuMmDevice) mm_device = g_object_new(FU_TYPE_MM_DEVICE, "context", ctx, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_physical_id(FU_DEVICE(mm_device), "/tmp");
	ret = fu_mm_device_set_autosuspend_delay(mm_device, 1500, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_mm_device_set_inhibited(mm_device, TRUE);
	g_assert_true(fu_mm_device_get_inhibited(mm_device));
	fu_mm_device_set_inhibited(mm_device, FALSE);
	g_assert_false(fu_mm_device_get_inhibited(mm_device));

	/* convert the instance IDs */
	ret = fu_mm_device_add_instance_id(mm_device,
					   "PCI\\SSVID_105B&SSPID_E142&REV_0000&CARRIER_CMCC",
					   &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_mm_device_add_instance_id(mm_device,
					   "PCI\\VID_17CB&PID_0308&REV_0000&CARRIER_CMCC",
					   &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* show what we've got */
	str = fu_device_to_string(FU_DEVICE(mm_device));
	g_debug("%s", str);

	/* check it all makes sense */
	g_assert_cmpint(fu_device_get_vid(FU_DEVICE(mm_device)), ==, 0x17CB);
	g_assert_cmpint(fu_device_get_pid(FU_DEVICE(mm_device)), ==, 0x0308);
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(mm_device),
						"PCI\\VID_17CB",
						FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(mm_device),
						"PCI\\VID_17CB&PID_0308",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						    FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(mm_device),
						"PCI\\VID_17CB&PID_0308&SUBSYS_105BE142",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						    FU_DEVICE_INSTANCE_FLAG_QUIRKS));

	/* add rev */
	fu_device_add_private_flag(FU_DEVICE(mm_device),
				   FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV);
	fu_mm_device_add_instance_id(mm_device,
				     "PCI\\VID_17CB&PID_0308&REV_0000&CARRIER_CMCC",
				     NULL);
	fu_mm_device_add_instance_id(mm_device,
				     "PCI\\SSVID_105B&SSPID_E142&REV_0000&CARRIER_CMCC",
				     NULL);
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(mm_device),
						"PCI\\VID_17CB&PID_0308&REV_0000",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						    FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(mm_device),
						"PCI\\VID_17CB&PID_0308&SUBSYS_105BE142&REV_0000",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						    FU_DEVICE_INSTANCE_FLAG_QUIRKS));

	/* add branch */
	fu_device_add_private_flag(FU_DEVICE(mm_device), FU_MM_DEVICE_FLAG_USE_BRANCH);
	fu_mm_device_add_instance_id(mm_device,
				     "PCI\\VID_17CB&PID_0308&REV_0000&CARRIER_CMCC",
				     NULL);
	fu_mm_device_add_instance_id(mm_device,
				     "PCI\\SSVID_105B&SSPID_E142&REV_0000&CARRIER_CMCC",
				     NULL);
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(mm_device),
						"PCI\\VID_17CB&PID_0308&REV_0000&CARRIER_CMCC",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						    FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_true(fu_device_has_instance_id(
	    FU_DEVICE(mm_device),
	    "PCI\\VID_17CB&PID_0308&SUBSYS_105BE142&REV_0000&CARRIER_CMCC",
	    FU_DEVICE_INSTANCE_FLAG_VISIBLE | FU_DEVICE_INSTANCE_FLAG_QUIRKS));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/mm/device", fu_mm_device_func);
	g_test_add_func("/mm/firehose/dev-image", fu_mm_mhi_firehose_dev_image_func);
	g_test_add_func("/mm/firehose/dev-image-ambiguous",
			fu_mm_mhi_firehose_dev_image_ambiguous_func);
	g_test_add_func("/mm/firehose/dev-image-legacy",
			fu_mm_mhi_firehose_dev_image_legacy_func);
	return g_test_run();
}
