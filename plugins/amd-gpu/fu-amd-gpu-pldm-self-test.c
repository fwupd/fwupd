/*
 * Copyright 2026 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-amd-gpu-pldm-firmware.h"
#include "fu-amd-gpu-pldm-struct.h"

#define FU_AMD_GPU_PLDM_TEST_PAYLOAD	"PAYLOAD!"
#define FU_AMD_GPU_PLDM_TEST_PAYLOAD_SZ 8

/*
 * Build a minimal but valid DSP0267 firmware package:
 *  - one firmware device ID record with a single PCI Vendor ID descriptor
 *  - one component image (8 bytes) located immediately after the header
 */
static GByteArray *
fu_amd_gpu_pldm_self_test_build_package(void)
{
	guint32 crc;
	GByteArray *buf = g_byte_array_new();
	g_autoptr(FuStructAmdGpuPldmHeader) hdr = fu_struct_amd_gpu_pldm_header_new();
	g_autoptr(FuStructAmdGpuPldmDeviceIdRecord) rec =
	    fu_struct_amd_gpu_pldm_device_id_record_new();
	g_autoptr(FuStructAmdGpuPldmDescriptor) desc = fu_struct_amd_gpu_pldm_descriptor_new();
	g_autoptr(FuStructAmdGpuPldmComponent) comp = fu_struct_amd_gpu_pldm_component_new();
	const guint8 bitmap = 0x01;
	const guint8 vendor_id[] = {0x02, 0x10}; /* 0x1002, little endian */

	/* package header information (identifier + format_revision are defaulted) */
	fu_struct_amd_gpu_pldm_header_set_header_size(hdr, 94);
	fu_struct_amd_gpu_pldm_header_set_component_bitmap_bit_length(hdr, 8);
	fu_struct_amd_gpu_pldm_header_set_version_string_type(hdr,
							      FU_AMD_GPU_PLDM_STRING_TYPE_ASCII);
	fu_struct_amd_gpu_pldm_header_set_version_string_length(hdr, 3);
	g_byte_array_append(buf, hdr->buf->data, hdr->buf->len);
	g_byte_array_append(buf, (const guint8 *)"1.0", 3);

	/* firmware device identification area: one record */
	g_byte_array_append(buf, (const guint8[]){0x01}, 1); /* DeviceIDRecordCount */
	fu_struct_amd_gpu_pldm_device_id_record_set_record_length(rec, 21);
	fu_struct_amd_gpu_pldm_device_id_record_set_descriptor_count(rec, 1);
	fu_struct_amd_gpu_pldm_device_id_record_set_version_string_type(
	    rec,
	    FU_AMD_GPU_PLDM_STRING_TYPE_ASCII);
	fu_struct_amd_gpu_pldm_device_id_record_set_version_string_length(rec, 3);
	g_byte_array_append(buf, rec->buf->data, rec->buf->len);
	g_byte_array_append(buf, &bitmap, 1);		    /* ApplicableComponents */
	g_byte_array_append(buf, (const guint8 *)"dev", 3); /* version string */
	fu_struct_amd_gpu_pldm_descriptor_set_descriptor_type(
	    desc,
	    FU_AMD_GPU_PLDM_DESCRIPTOR_TYPE_PCI_VENDOR_ID);
	fu_struct_amd_gpu_pldm_descriptor_set_descriptor_length(desc, 2);
	g_byte_array_append(buf, desc->buf->data, desc->buf->len);
	g_byte_array_append(buf, vendor_id, sizeof(vendor_id));

	/* component image information area: one component */
	g_byte_array_append(buf, (const guint8[]){0x01, 0x00}, 2); /* ComponentImageCount */
	fu_struct_amd_gpu_pldm_component_set_classification(comp, 0x000A);
	fu_struct_amd_gpu_pldm_component_set_identifier(comp, 0x0001);
	fu_struct_amd_gpu_pldm_component_set_comparison_stamp(comp, 0xFFFFFFFF);
	fu_struct_amd_gpu_pldm_component_set_location_offset(comp, 94);
	fu_struct_amd_gpu_pldm_component_set_size(comp, FU_AMD_GPU_PLDM_TEST_PAYLOAD_SZ);
	fu_struct_amd_gpu_pldm_component_set_version_string_type(comp,
								 FU_AMD_GPU_PLDM_STRING_TYPE_ASCII);
	fu_struct_amd_gpu_pldm_component_set_version_string_length(comp, 5);
	g_byte_array_append(buf, comp->buf->data, comp->buf->len);
	g_byte_array_append(buf, (const guint8 *)"comp1", 5);

	/* package header checksum over everything preceding it */
	g_assert_cmpuint(buf->len, ==, 90);
	crc = fu_crc32(FU_CRC_KIND_B32_STANDARD, buf->data, buf->len);
	fu_byte_array_append_uint32(buf, crc, G_LITTLE_ENDIAN);

	/* the component payload follows the header */
	g_byte_array_append(buf,
			    (const guint8 *)FU_AMD_GPU_PLDM_TEST_PAYLOAD,
			    FU_AMD_GPU_PLDM_TEST_PAYLOAD_SZ);
	return buf;
}

static void
fu_amd_gpu_pldm_firmware_parse_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_amd_gpu_pldm_firmware_new();
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GByteArray) buf = fu_amd_gpu_pldm_self_test_build_package();
	g_autoptr(GBytes) blob = g_bytes_new(buf->data, buf->len);
	g_autoptr(GBytes) img_blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *version = NULL;

	ret = fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* package version string */
	g_assert_cmpstr(fu_firmware_get_version(firmware), ==, "1.0");

	/* one component image, exposed as a child firmware */
	img = fu_firmware_get_image_by_idx(firmware, 0x1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img);
	g_assert_cmpstr(fu_firmware_get_version(img), ==, "comp1");
	g_assert_cmpint(fu_firmware_get_addr(img), ==, 94);
	img_blob = fu_firmware_get_bytes(img, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_blob);
	g_assert_cmpuint(g_bytes_get_size(img_blob), ==, FU_AMD_GPU_PLDM_TEST_PAYLOAD_SZ);
}

static void
fu_amd_gpu_pldm_firmware_bad_checksum_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware1 = fu_amd_gpu_pldm_firmware_new();
	g_autoptr(FuFirmware) firmware2 = fu_amd_gpu_pldm_firmware_new();
	g_autoptr(GByteArray) buf = fu_amd_gpu_pldm_self_test_build_package();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* corrupt a byte covered by the header checksum */
	buf->data[36] = 'X';
	blob = g_bytes_new(buf->data, buf->len);

	/* rejected without the flag */
	ret = fu_firmware_parse_bytes(firmware1, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
	g_clear_error(&error);

	/* accepted when the checksum is ignored */
	ret = fu_firmware_parse_bytes(firmware2,
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_amd_gpu_pldm_firmware_bad_magic_func(void)
{
	gboolean ret;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(GByteArray) buf = fu_amd_gpu_pldm_self_test_build_package();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* corrupt the PackageHeaderIdentifier UUID */
	buf->data[0] = 0x00;
	blob = g_bytes_new(buf->data, buf->len);
	stream = fu_memory_input_stream_new_from_bytes(blob);

	ret = fu_struct_amd_gpu_pldm_header_validate_stream(stream, 0x0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/amd-gpu/pldm/parse", fu_amd_gpu_pldm_firmware_parse_func);
	g_test_add_func("/amd-gpu/pldm/bad-checksum", fu_amd_gpu_pldm_firmware_bad_checksum_func);
	g_test_add_func("/amd-gpu/pldm/bad-magic", fu_amd_gpu_pldm_firmware_bad_magic_func);
	return g_test_run();
}
