/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-efi-lz77-decompressor.h"
#include "fu-efi-x509-signature-private.h"

static void
fu_efi_x509_signature_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuEfiX509Signature) sig = fu_efi_x509_signature_new();
	g_autoptr(FuEfiX509Device) device = fu_efi_x509_device_new(ctx, sig);
	g_autoptr(GError) error = NULL;

	fu_firmware_set_id(FU_FIRMWARE(sig), "0000000000000000000000000000000000000000");
	fu_efi_x509_signature_set_issuer(sig, "C=UK,O=fwupd,CN=fwupd root CA 2012");
	fu_efi_x509_signature_set_subject(sig, "C=UK,O=Hughski Ltd.,CN=Hughski Ltd. KEK CA 2012");

	/* get issuer */
	g_assert_cmpstr(fu_efi_x509_signature_get_issuer(sig),
			==,
			"C=UK,O=fwupd,CN=fwupd root CA 2012");
	g_assert_cmpstr(fu_efi_x509_signature_get_subject(sig),
			==,
			"C=UK,O=Hughski Ltd.,CN=Hughski Ltd. KEK CA 2012");
	g_assert_cmpstr(fu_efi_x509_signature_get_subject_name(sig), ==, "Hughski KEK CA");
	g_assert_cmpstr(fu_efi_x509_signature_get_subject_vendor(sig), ==, "Hughski");
	g_assert_cmpint(fu_firmware_get_version_raw(FU_FIRMWARE(sig)), ==, 2012);
	g_assert_cmpstr(fu_firmware_get_version(FU_FIRMWARE(sig)), ==, "2012");

	/* create a device from the certificate */
	ret = fu_device_probe(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_device_get_version_raw(FU_DEVICE(device)), ==, 2012);
	g_assert_cmpstr(fu_device_get_version(FU_DEVICE(device)), ==, "2012");
	g_assert_cmpstr(fu_device_get_name(FU_DEVICE(device)), ==, "KEK CA");
	g_assert_cmpstr(fu_device_get_vendor(FU_DEVICE(device)), ==, "Hughski");
	g_assert_true(fu_device_has_instance_id(FU_DEVICE(device),
						"UEFI\\VENDOR_Hughski&NAME_Hughski-KEK-CA",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_true(
	    fu_device_has_instance_id(FU_DEVICE(device),
				      "UEFI\\CRT_0000000000000000000000000000000000000000",
				      FU_DEVICE_INSTANCE_FLAG_VISIBLE));
}

static void
fu_efi_variable_authentication2_func(void)
{
	FuFirmware *signer;
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) efi_x509 = NULL;
	g_autoptr(FuFirmware) firmware = g_object_new(FU_TYPE_EFI_VARIABLE_AUTHENTICATION2, NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GPtrArray) signers = NULL;

	/* parse file */
	fn = g_test_build_filename(G_TEST_DIST, "tests", "KEKUpdate.bin", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing KEKUpdate.bin");
		return;
	}
	file = g_file_new_for_path(fn);
	ret = fu_firmware_parse_file(firmware, file, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str = fu_firmware_to_string(firmware);
	g_debug("%s", str);

	/* get EFI sig */
	efi_x509 = fu_firmware_get_image_by_id(firmware,
					       "dec64d7746d983db3774829a00bf829d9f19e9cf",
					       &error);
	g_assert_no_error(error);
	g_assert_nonnull(efi_x509);
	g_assert_cmpstr("C=US,O=Microsoft Corporation,CN=Microsoft RSA Devices Root CA 2021",
			==,
			fu_efi_x509_signature_get_issuer(FU_EFI_X509_SIGNATURE(efi_x509)));
	g_assert_cmpstr("C=US,O=Microsoft Corporation,CN=Microsoft Corporation KEK 2K CA 2023",
			==,
			fu_efi_x509_signature_get_subject(FU_EFI_X509_SIGNATURE(efi_x509)));

	/* get signer */
	signers =
	    fu_efi_variable_authentication2_get_signers(FU_EFI_VARIABLE_AUTHENTICATION2(firmware));
	g_assert_nonnull(signers);
	g_assert_cmpint(signers->len, ==, 1);

	signer = g_ptr_array_index(signers, 0);
	g_assert_cmpstr("CN=DO NOT TRUST - AMI Test PK",
			==,
			fu_x509_certificate_get_issuer(FU_X509_CERTIFICATE(signer)));
	g_assert_cmpstr("CN=DO NOT TRUST - AMI Test PK",
			==,
			fu_x509_certificate_get_subject(FU_X509_CERTIFICATE(signer)));
}

static void
fu_efi_signature_list_func(void)
{
	FuEfiX509Signature *sig;
	gboolean ret;
	g_autoptr(FuEfiX509Signature) sig2022 = fu_efi_x509_signature_new();
	g_autoptr(FuEfiX509Signature) sig2023 = fu_efi_x509_signature_new();
	g_autoptr(FuEfiX509Signature) sig2024 = fu_efi_x509_signature_new();
	g_autoptr(FuFirmware) siglist = fu_efi_signature_list_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) sigs_newest = NULL;

	fu_efi_x509_signature_set_subject(sig2022, "C=UK,O=Hughski,CN=Hughski Ltd. KEK CA 2022");
	fu_efi_x509_signature_set_subject(sig2023, "C=UK,O=Hughski,CN=Hughski Ltd. KEK CA 2023");
	fu_efi_x509_signature_set_subject(sig2024, "C=UK,O=Hughski,CN=Hughski Ltd. KEK CA 2024");

	/* 2022 -> 2024 -> 2023 */
	ret = fu_firmware_add_image(siglist, FU_FIRMWARE(sig2022), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_firmware_add_image(siglist, FU_FIRMWARE(sig2024), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_firmware_add_image(siglist, FU_FIRMWARE(sig2023), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* only one */
	sigs_newest = fu_efi_signature_list_get_newest(FU_EFI_SIGNATURE_LIST(siglist));
	g_assert_cmpint(sigs_newest->len, ==, 1);
	sig = g_ptr_array_index(sigs_newest, 0);
	g_assert_cmpint(fu_firmware_get_version_raw(FU_FIRMWARE(sig)), ==, 2024);
}

static void
fu_efi_lz77_decompressor_func(void)
{
	gboolean ret;
	g_autofree gchar *csum_legacy = NULL;
	g_autofree gchar *csum_tiano = NULL;
	g_autofree gchar *filename_legacy = NULL;
	g_autofree gchar *filename_tiano = NULL;
	g_autoptr(FuFirmware) lz77_decompressor_legacy = fu_efi_lz77_decompressor_new();
	g_autoptr(FuFirmware) lz77_decompressor_tiano = fu_efi_lz77_decompressor_new();
	g_autoptr(GBytes) blob_legacy2 = NULL;
	g_autoptr(GBytes) blob_legacy = NULL;
	g_autoptr(GBytes) blob_tiano2 = NULL;
	g_autoptr(GBytes) blob_tiano = NULL;
	g_autoptr(GError) error = NULL;

	filename_tiano = g_test_build_filename(G_TEST_DIST, "tests", "efi-lz77-tiano.bin", NULL);
	if (!g_file_test(filename_tiano, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing efi-lz77-tiano.bin");
		return;
	}
	blob_tiano = fu_bytes_get_contents(filename_tiano, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_tiano);
	g_assert_cmpint(g_bytes_get_size(blob_tiano), ==, 144);
	ret = fu_firmware_parse_bytes(lz77_decompressor_tiano,
				      blob_tiano,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob_tiano2 = fu_firmware_get_bytes(lz77_decompressor_tiano, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_tiano2);
	g_assert_cmpint(g_bytes_get_size(blob_tiano2), ==, 276);
	csum_tiano = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, blob_tiano2);
	g_assert_cmpstr(csum_tiano, ==, "40f7fbaff684a6bcf67c81b3079422c2529741e1");

	filename_legacy = g_test_build_filename(G_TEST_DIST, "tests", "efi-lz77-legacy.bin", NULL);
	if (!g_file_test(filename_tiano, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing efi-lz77-legacy.bin");
		return;
	}
	blob_legacy = fu_bytes_get_contents(filename_legacy, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_legacy);
	g_assert_cmpint(g_bytes_get_size(blob_legacy), ==, 144);
	ret = fu_firmware_parse_bytes(lz77_decompressor_legacy,
				      blob_tiano,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob_legacy2 = fu_firmware_get_bytes(lz77_decompressor_legacy, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_legacy2);
	g_assert_cmpint(g_bytes_get_size(blob_legacy2), ==, 276);
	csum_legacy = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, blob_legacy2);
	g_assert_cmpstr(csum_legacy, ==, "40f7fbaff684a6bcf67c81b3079422c2529741e1");
}

static void
fu_efi_load_option_path_func(void)
{
	const gchar *tmp;
	gboolean ret;
	g_autofree gchar *blobstr = NULL;
	g_autoptr(FuEfiDevicePathList) devpathlist = fu_efi_device_path_list_new();
	g_autoptr(FuEfiLoadOption) loadopt = fu_efi_load_option_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	g_assert_cmpint(fu_efi_load_option_get_kind(loadopt), ==, FU_EFI_LOAD_OPTION_KIND_UNKNOWN);
	fu_efi_load_option_set_metadata(loadopt, FU_EFI_LOAD_OPTION_METADATA_PATH, "/foo");
	g_assert_cmpint(fu_efi_load_option_get_kind(loadopt), ==, FU_EFI_LOAD_OPTION_KIND_PATH);

	tmp = fu_efi_load_option_get_metadata(loadopt, FU_EFI_LOAD_OPTION_METADATA_PATH, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(tmp, ==, "/foo");

	fu_firmware_set_id(FU_FIRMWARE(loadopt), "id");
	ret = fu_firmware_add_image(FU_FIRMWARE(loadopt), FU_FIRMWARE(devpathlist), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob = fu_firmware_write(FU_FIRMWARE(loadopt), &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	blobstr = fu_bytes_to_string(blob);
	g_assert_cmpstr(blobstr, ==, "0100000004006900640000007fff04005c002f0066006f006f000000");
}

static void
fu_efi_load_option_hive_func(void)
{
	gboolean ret;
	g_autofree gchar *blobstr = NULL;
	g_autoptr(FuEfiDevicePathList) devpathlist = fu_efi_device_path_list_new();
	g_autoptr(FuEfiLoadOption) loadopt = fu_efi_load_option_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	g_assert_cmpint(fu_efi_load_option_get_kind(loadopt), ==, FU_EFI_LOAD_OPTION_KIND_UNKNOWN);
	fu_efi_load_option_set_metadata(loadopt, FU_EFI_LOAD_OPTION_METADATA_PATH, "/foo");
	fu_efi_load_option_set_metadata(loadopt, FU_EFI_LOAD_OPTION_METADATA_CMDLINE, "noacpi");
	g_assert_cmpint(fu_efi_load_option_get_kind(loadopt), ==, FU_EFI_LOAD_OPTION_KIND_HIVE);

	fu_firmware_set_id(FU_FIRMWARE(loadopt), "id");
	ret = fu_firmware_add_image(FU_FIRMWARE(loadopt), FU_FIRMWARE(devpathlist), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob = fu_firmware_write(FU_FIRMWARE(loadopt), &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	g_assert_cmpint(g_bytes_get_size(blob), ==, 512);
	blobstr = fu_bytes_to_string(blob);

	/* get rid of extra NUL butes */
	blobstr[120] = '\0';
	g_assert_cmpstr(blobstr,
			==,
			"0100000004006900640000007fff04004849564501020b0f4a6ea20405000000706174685c"
			"2f666f6f0706000000636d646c696e656e6f6163706900");
}

static void
fu_efi_load_option_func(void)
{
	g_autoptr(FuEfivars) efivars = fu_efivars_new();
	/*
	 * 0000 = Linux-Firmware-Updater
	 * 0001 = Fedora
	 * 0002 = Windows Boot Manager
	 */
	for (guint16 i = 0; i < 3; i++) {
		g_autoptr(GError) error = NULL;
		g_autoptr(FuEfiLoadOption) load_option =
		    fu_efivars_get_boot_entry(efivars, i, &error);
		g_autoptr(GBytes) fw = NULL;
		g_autofree gchar *str = NULL;

		if (load_option == NULL) {
			g_debug("failed: %s", error->message);
			continue;
		}
		str = fu_firmware_to_string(FU_FIRMWARE(load_option));
		g_debug("%s", str);
		fw = fu_firmware_write(FU_FIRMWARE(load_option), &error);
		g_assert_no_error(error);
		g_assert_nonnull(fw);
	}
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/efi/load-option", fu_efi_load_option_func);
	g_test_add_func("/fwupd/efi/load-option/path", fu_efi_load_option_path_func);
	g_test_add_func("/fwupd/efi/load-option/hive", fu_efi_load_option_hive_func);
	g_test_add_func("/fwupd/efi/x509-signature", fu_efi_x509_signature_func);
	g_test_add_func("/fwupd/efi/signature-list", fu_efi_signature_list_func);
	g_test_add_func("/fwupd/efi/variable-authentication2",
			fu_efi_variable_authentication2_func);
	g_test_add_func("/fwupd/efi/lz77/decompressor", fu_efi_lz77_decompressor_func);
	return g_test_run();
}
