/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-efi-signature-private.h"
#include "fu-efi-x509-signature-private.h"
#include "fu-uefi-dbx-common.h"
#include "fu-uefi-dbx-device.h"
#include "fu-uefi-device-private.h"

typedef struct {
	const gchar *checksums[5];
	const gchar *version;
	const gchar *checksum;
	const gchar *last_owner;
	const gchar *reference_checksums[5];
	gboolean write_reference;
	gboolean corrupt_reference;
} FuUefiDbxVersionTest;

static void
fu_uefi_dbx_version_func(gconstpointer user_data)
{
	const FuUefiDbxVersionTest *test = user_data;
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_CACHE | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_UEFI_DBX_DEVICE, "context", ctx, NULL);
	g_autoptr(FuEfiX509Signature) sig_kek = fu_efi_x509_signature_new();
	g_autoptr(FuFirmware) siglist_dbx = fu_efi_signature_list_new();
	g_autoptr(FuFirmware) siglist_kek = fu_efi_signature_list_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) blob_dbx = NULL;
	g_autoptr(GBytes) blob_kek = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_GNUTLS
	/* nt possible to generate a certificate */
	g_test_skip("this test requires GnuTLS");
	return;
#endif

	/* cache writes and reads must be isolated from the system state */
	tmpdir = fu_temporary_directory_new("uefi-dbx", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_PKG, tmpdir);

	/* do not save silo */
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, g_test_get_dir(G_TEST_DIST));
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create a plausible KEK */
	fu_efi_x509_signature_set_issuer(sig_kek, "C=UK,O=fwupd,CN=fwupd root CA 2012");
	fu_efi_x509_signature_set_subject(sig_kek,
					  "C=UK,O=Hughski Ltd.,CN=Hughski Ltd. KEK CA 2012");
	fu_firmware_add_image(siglist_kek, FU_FIRMWARE(sig_kek), NULL);
	blob_kek = fu_firmware_write(siglist_kek, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_kek);
	fu_uefi_device_set_guid(FU_UEFI_DEVICE(device), FU_EFIVARS_GUID_EFI_GLOBAL);
	fu_uefi_device_set_name(FU_UEFI_DEVICE(device), "KEK");
	ret = fu_uefi_device_set_efivar_bytes(FU_UEFI_DEVICE(device),
					      FU_EFIVARS_GUID_EFI_GLOBAL,
					      "KEK",
					      blob_kek,
					      FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	if (test->reference_checksums[0] != NULL) {
		g_autofree gchar *filename = NULL;
		g_autoptr(FuFirmware) reference =
		    g_object_new(FU_TYPE_EFI_VARIABLE_AUTHENTICATION2, NULL);
		g_autoptr(GBytes) blob = NULL;

		for (guint i = 0; test->reference_checksums[i] != NULL; i++) {
			g_autoptr(FuEfiSignature) sig =
			    fu_efi_signature_new(FU_EFI_SIGNATURE_KIND_SHA256);
			g_autoptr(GBytes) csum =
			    fu_bytes_from_string(test->reference_checksums[i], &error);
			g_assert_no_error(error);
			g_assert_nonnull(csum);
			fu_efi_signature_set_owner(sig, FU_EFI_SIGNATURE_GUID_MICROSOFT);
			fu_firmware_set_bytes(FU_FIRMWARE(sig), csum);
			fu_firmware_add_image(reference, FU_FIRMWARE(sig), NULL);
		}
		blob = fu_firmware_write(reference, &error);
		g_assert_no_error(error);
		g_assert_nonnull(blob);
		filename = fu_context_build_filename(ctx,
						     &error,
						     FU_PATH_KIND_LOCALSTATEDIR_PKG,
						     "uefi-dbx.bin",
						     NULL);
		g_assert_no_error(error);
		g_assert_nonnull(filename);
		if (test->write_reference) {
			g_autoptr(FuFirmware) parsed =
			    g_object_new(FU_TYPE_EFI_VARIABLE_AUTHENTICATION2, NULL);
			g_autoptr(GBytes) cached = NULL;
			ret = fu_firmware_parse_bytes(parsed,
						      blob,
						      0,
						      FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
						      &error);
			g_assert_no_error(error);
			g_assert_true(ret);
			ret = fu_device_write_firmware(device,
						       parsed,
						       progress,
						       FWUPD_INSTALL_FLAG_NONE,
						       &error);
			g_assert_no_error(error);
			g_assert_true(ret);
			cached = fu_bytes_get_contents(filename, &error);
			g_assert_no_error(error);
			g_assert_nonnull(cached);
			g_assert_true(g_bytes_equal(cached, blob));
		} else {
			if (test->corrupt_reference) {
				g_clear_pointer(&blob, g_bytes_unref);
				blob = g_bytes_new_static("invalid", 7);
			}
			ret = fu_bytes_set_contents(filename, blob, &error);
			g_assert_no_error(error);
			g_assert_true(ret);
		}
	}

	/* create a plausible dbx in the supplied order */
	for (guint i = 0; test->checksums[i] != NULL; i++) {
		g_autoptr(FuEfiSignature) sig = fu_efi_signature_new(FU_EFI_SIGNATURE_KIND_SHA256);
		g_autoptr(GBytes) csum = fu_bytes_from_string(test->checksums[i], &error);
		g_assert_no_error(error);
		g_assert_nonnull(csum);
		fu_efi_signature_set_owner(sig,
					   test->checksums[i + 1] == NULL &&
						   test->last_owner != NULL
					       ? test->last_owner
					       : FU_EFI_SIGNATURE_GUID_MICROSOFT);
		fu_firmware_set_bytes(FU_FIRMWARE(sig), csum);
		fu_firmware_add_image(siglist_dbx, FU_FIRMWARE(sig), NULL);
	}

	blob_dbx = fu_firmware_write(siglist_dbx, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_dbx);
	ret = fu_uefi_device_set_efivar_bytes(FU_UEFI_DEVICE(device),
					      FU_EFIVARS_GUID_SECURITY_DATABASE,
					      "dbx",
					      blob_dbx,
					      FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* detect version number */
	ret = fu_device_probe(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_version(device), ==, test->version);
	g_assert_cmpuint(fu_device_get_checksums(device)->len, ==, 1);
	g_assert_true(fu_device_has_checksum(device, test->checksum));
}

static void
fu_uefi_dbx_zero_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_UEFI_DBX_DEVICE, "context", ctx, NULL);
	g_autoptr(FuEfiSignature) sig = fu_efi_signature_new(FU_EFI_SIGNATURE_KIND_SHA256);
	g_autoptr(FuFirmware) siglist = fu_efi_signature_list_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) csum = NULL;
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* zero hash = empty */
	csum =
	    fu_bytes_from_string("0000000000000000000000000000000000000000000000000000000000000000",
				 &error);
	g_assert_no_error(error);
	g_assert_nonnull(csum);
	fu_firmware_set_bytes(FU_FIRMWARE(sig), csum);
	fu_efi_signature_set_owner(sig, FU_EFI_SIGNATURE_GUID_ZERO);
	fu_firmware_add_image(siglist, FU_FIRMWARE(sig), NULL);
	blob = fu_firmware_write(siglist, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	/* create a plausible KEK */
	fu_uefi_device_set_guid(FU_UEFI_DEVICE(device), FU_EFIVARS_GUID_EFI_GLOBAL);
	fu_uefi_device_set_name(FU_UEFI_DEVICE(device), "KEK");
	ret = fu_uefi_device_set_efivar_bytes(FU_UEFI_DEVICE(device),
					      FU_EFIVARS_GUID_EFI_GLOBAL,
					      "KEK",
					      blob,
					      FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create an "empty" dbx */
	ret = fu_uefi_device_set_efivar_bytes(FU_UEFI_DEVICE(device),
					      FU_EFIVARS_GUID_SECURITY_DATABASE,
					      "dbx",
					      blob,
					      FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* detect version number */
	ret = fu_device_probe(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_device_get_version_raw(device), ==, 0);
	g_assert_cmpstr(fu_device_get_version(device), ==, "0");
}

static void
fu_efi_image_func(void)
{
	struct {
		const gchar *basename;
		const gchar *checksum;
	} map[] = {
	    {"bootmgr.efi", "fd26aad248cc1e21e0c6b453212b2b309f7e221047bf22500ed0f8ce30bd1610"},
	    {"fwupdx64-2.efi", "6e0f01e7018c90a1e3d24908956fbeffd29a620c6c5f3ffa3feb2f2802ed4448"},
	};
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		gboolean ret;
		g_autofree gchar *csum = NULL;
		g_autofree gchar *fn = NULL;
		g_autoptr(FuFirmware) firmware = fu_pefile_firmware_new();
		g_autoptr(GError) error = NULL;
		g_autoptr(GFile) file = NULL;

		fn = g_test_build_filename(G_TEST_DIST, "tests", map[i].basename, NULL);
		file = g_file_new_for_path(fn);
		if (!g_file_query_exists(file, NULL)) {
			g_autofree gchar *msg =
			    g_strdup_printf("failed to find file %s", map[i].basename);
			g_test_skip(msg);
			return;
		}
		ret = fu_firmware_parse_file(firmware, file, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
		if (!ret)
			g_prefix_error(&error, "%s: ", map[i].basename);
		g_assert_no_error(error);
		g_assert_true(ret);

		csum = fu_firmware_get_checksum(firmware, G_CHECKSUM_SHA256, &error);
		g_assert_no_error(error);
		g_assert_nonnull(csum);
		g_assert_cmpstr(csum, ==, map[i].checksum);
	}
}

static void
fu_uefi_dbx_not_present_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_UEFI_DBX_DEVICE, "context", ctx, NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) ms_blob = NULL;
	g_autoptr(FuFirmware) ms_siglist = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autofree gchar *ms_kek_filename = NULL;
	g_autofree gchar *ms_kek_xml = NULL;

	/* do not save silo */
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create a KEK with Microsoft's signature */
	ms_kek_filename =
	    g_test_build_filename(G_TEST_DIST, "tests", "efi-signature-list.builder.xml", NULL);
	g_assert_nonnull(ms_kek_filename);

	ret = g_file_get_contents(ms_kek_filename, &ms_kek_xml, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ms_siglist = fu_firmware_new_from_xml(ms_kek_xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(ms_siglist);

	ms_blob = fu_firmware_write(ms_siglist, &error);
	g_assert_no_error(error);
	g_assert_nonnull(ms_blob);

	fu_uefi_device_set_guid(FU_UEFI_DEVICE(device), FU_EFIVARS_GUID_EFI_GLOBAL);
	fu_uefi_device_set_name(FU_UEFI_DEVICE(device), "KEK");
	ret = fu_uefi_device_set_efivar_bytes(FU_UEFI_DEVICE(device),
					      FU_EFIVARS_GUID_EFI_GLOBAL,
					      "KEK",
					      ms_blob,
					      FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_device_probe(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_uefi_dbx_prepare_firmware_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx =
	    fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS | FU_CONTEXT_FLAG_DUMMY_EFIVARS);
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_UEFI_DBX_DEVICE, "context", ctx, NULL);
	g_autoptr(FuEfiSignature) sig = fu_efi_signature_new(FU_EFI_SIGNATURE_KIND_SHA256);
	g_autoptr(FuFirmware) auth2 = g_object_new(FU_TYPE_EFI_VARIABLE_AUTHENTICATION2, NULL);
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GPtrArray) imgs = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_out = NULL;
	g_autoptr(GBytes) csum = NULL;
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* build a plausible signed dbx update */
	csum =
	    fu_bytes_from_string("418ad44c79e3fddd6a0574b24fcf0fb8fee4b3ff2be635d21a5c0852bdea635c",
				 &error);
	g_assert_no_error(error);
	g_assert_nonnull(csum);
	fu_firmware_set_bytes(FU_FIRMWARE(sig), csum);
	fu_firmware_add_image(auth2, FU_FIRMWARE(sig), NULL);
	blob = fu_firmware_write(auth2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	/* parse it back through the device */
	stream = FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(blob));
	firmware = fu_device_prepare_firmware(device,
					      stream,
					      progress,
					      FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
					      &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);

	/* the concrete type has to survive, so that other plugins can introspect the
	 * payload -- see fu_snapd_uefi_plugin_composite_peek_firmware() */
	g_assert_true(FU_IS_EFI_VARIABLE_AUTHENTICATION2(firmware));
	imgs = fu_firmware_get_images(firmware);
	g_assert_cmpint(imgs->len, ==, 1);

	/* ...and the payload has to be byte-identical, as it is written verbatim into the
	 * efivar by fu_uefi_dbx_device_write_firmware() */
	blob_out = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_out);
	g_assert_true(g_bytes_equal(blob, blob_out));
}

static void
fu_uefi_dbx_validate_filename_func(void)
{
	gboolean ret;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *fn_pe = NULL;
	g_autofree gchar *fn_xml = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuEfiSignature) sig = fu_efi_signature_new(FU_EFI_SIGNATURE_KIND_SHA256);
	g_autoptr(FuFirmware) pefile = NULL;
	g_autoptr(FuFirmware) pefile_tmp = fu_pefile_firmware_new();
	g_autoptr(FuFirmware) siglist = fu_efi_signature_list_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) blob_pe = NULL;
	g_autoptr(GBytes) csum = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_pe = NULL;

	/* so FuPefileFirmware et al. can be resolved from the builder XML */
	fu_context_add_firmware_gtypes(ctx);

	/* build a plausible PE file using a builder.xml */
	fn_xml = g_test_build_filename(G_TEST_DIST, "tests", "pefile.builder.xml", NULL);
	g_assert_nonnull(fn_xml);
	pefile = fu_firmware_new_from_filename(fn_xml, &error);
	g_assert_no_error(error);
	g_assert_nonnull(pefile);
	blob_pe = fu_firmware_write(pefile, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_pe);

	/* save it to disk so it can be re-parsed as an on-ESP binary */
	tmpdir = fu_temporary_directory_new("uefi-dbx", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fn_pe = fu_temporary_directory_build(tmpdir, "bootx64.efi", NULL);
	g_assert_nonnull(fn_pe);
	ret = fu_bytes_set_contents(fn_pe, blob_pe, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* get the Authenticode checksum of the written PE file */
	file_pe = g_file_new_for_path(fn_pe);
	ret = fu_firmware_parse_file(pefile_tmp, file_pe, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	checksum = fu_firmware_get_checksum(pefile_tmp, G_CHECKSUM_SHA256, &error);
	g_assert_no_error(error);
	g_assert_nonnull(checksum);

	/* a dbx that does not contain the checksum validates fine */
	ret = fu_uefi_dbx_signature_list_validate_filename(ctx,
							   FU_EFI_SIGNATURE_LIST(siglist),
							   fn_pe,
							   FU_FIRMWARE_PARSE_FLAG_NONE,
							   &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add the Authenticode checksum of the PE file into the installed dbx */
	csum = fu_bytes_from_string(checksum, &error);
	g_assert_no_error(error);
	g_assert_nonnull(csum);
	fu_efi_signature_set_owner(sig, FU_EFI_SIGNATURE_GUID_MICROSOFT);
	fu_firmware_set_bytes(FU_FIRMWARE(sig), csum);
	fu_firmware_add_image(siglist, FU_FIRMWARE(sig), NULL);

	/* now the checksum is present in the dbx, validation must fail */
	ret = fu_uefi_dbx_signature_list_validate_filename(ctx,
							   FU_EFI_SIGNATURE_LIST(siglist),
							   fn_pe,
							   FU_FIRMWARE_PARSE_FLAG_NONE,
							   &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NEEDS_USER_ACTION);
	g_assert_false(ret);
	g_clear_error(&error);

	/* a file that is not a PE file is silently ignored */
	ret = fu_uefi_dbx_signature_list_validate_filename(ctx,
							   FU_EFI_SIGNATURE_LIST(siglist),
							   fn_xml,
							   FU_FIRMWARE_PARSE_FLAG_NONE,
							   &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/uefi-dbx/image", fu_efi_image_func);
	g_test_add_func("/uefi-dbx/zero", fu_uefi_dbx_zero_func);
	{
		const gchar *old =
		    "13a1f37bedfb5417b6b737e2a3816c8fd587d74d836914b2b2edc9fd6ca30e58";
		const gchar *middle =
		    "96275dfd6282a522b011177ee049296952ac794832091f937fbbf92869028629";
		const gchar *latest =
		    "2ea557c44b83c0ad6b71efb7edcc18b6337ad1c1d682155dd9451b051b62ff40";
		const gchar *unknown =
		    "1111111111111111111111111111111111111111111111111111111111111111";
		static FuUefiDbxVersionTest tests[12];
		tests[0] = (FuUefiDbxVersionTest){
		    .checksums = {old, middle, latest},
		    .version = "20260707",
		    .checksum = latest,
		};
		tests[1] = (FuUefiDbxVersionTest){
		    .checksums = {latest, middle, old},
		    .version = "20260707",
		    .checksum = latest,
		    .reference_checksums = {old, middle, latest},
		};
		tests[2] = (FuUefiDbxVersionTest){
		    .checksums = {middle, latest, old},
		    .version = "20260707",
		    .checksum = latest,
		    .reference_checksums = {old, middle, latest},
		};
		tests[3] = (FuUefiDbxVersionTest){
		    .checksums = {latest, unknown},
		    .version = NULL,
		    .checksum = unknown,
		    .reference_checksums = {latest},
		};
		tests[4] = (FuUefiDbxVersionTest){
		    .checksums = {unknown},
		    .version = NULL,
		    .checksum = unknown,
		};
		tests[5] = (FuUefiDbxVersionTest){
		    .checksums = {middle, latest},
		    .version = "20260402",
		    .checksum = middle,
		    .last_owner = "00112233-4455-6677-8899-aabbccddeeff",
		};
		tests[6] = (FuUefiDbxVersionTest){
		    .checksums = {latest, old},
		    .version = "20230501",
		    .checksum = old,
		    .reference_checksums = {old, middle, latest},
		};
		tests[7] = (FuUefiDbxVersionTest){
		    .checksums = {latest, old},
		    .version = "20230501",
		    .checksum = old,
		    .reference_checksums = {old, unknown, latest},
		};
		tests[8] = (FuUefiDbxVersionTest){
		    .checksums = {old, middle, latest},
		    .version = "20260707",
		    .checksum = latest,
		    .reference_checksums = {old, middle},
		};
		tests[9] = (FuUefiDbxVersionTest){
		    .checksums = {latest, middle, old},
		    .version = "20230501",
		    .checksum = old,
		    .reference_checksums = {old, middle, latest},
		    .corrupt_reference = TRUE,
		};
		tests[10] = (FuUefiDbxVersionTest){
		    .checksums = {latest, middle, old},
		    .version = "20230501",
		    .checksum = old,
		};
		tests[11] = (FuUefiDbxVersionTest){
		    .checksums = {middle, latest, old},
		    .version = "20260707",
		    .checksum = latest,
		    .reference_checksums = {old, middle, latest},
		    .write_reference = TRUE,
		};
		g_test_add_data_func("/uefi-dbx/version/ascending",
				     &tests[0],
				     fu_uefi_dbx_version_func);
		g_test_add_data_func("/uefi-dbx/version/descending",
				     &tests[1],
				     fu_uefi_dbx_version_func);
		g_test_add_data_func("/uefi-dbx/version/reordered",
				     &tests[2],
				     fu_uefi_dbx_version_func);
		g_test_add_data_func("/uefi-dbx/version/unknown-tail",
				     &tests[3],
				     fu_uefi_dbx_version_func);
		g_test_add_data_func("/uefi-dbx/version/unknown",
				     &tests[4],
				     fu_uefi_dbx_version_func);
		g_test_add_data_func("/uefi-dbx/version/non-microsoft",
				     &tests[5],
				     fu_uefi_dbx_version_func);
		g_test_add_data_func("/uefi-dbx/version/missing-marker",
				     &tests[6],
				     fu_uefi_dbx_version_func);
		g_test_add_data_func("/uefi-dbx/version/missing-revocation",
				     &tests[7],
				     fu_uefi_dbx_version_func);
		g_test_add_data_func("/uefi-dbx/version/stale-cache",
				     &tests[8],
				     fu_uefi_dbx_version_func);
		g_test_add_data_func("/uefi-dbx/version/corrupt-cache",
				     &tests[9],
				     fu_uefi_dbx_version_func);
		g_test_add_data_func("/uefi-dbx/version/no-cache",
				     &tests[10],
				     fu_uefi_dbx_version_func);
		g_test_add_data_func("/uefi-dbx/version/write-cache",
				     &tests[11],
				     fu_uefi_dbx_version_func);
	}
	g_test_add_func("/uefi-dbx/not-present", fu_uefi_dbx_not_present_func);
	g_test_add_func("/uefi-dbx/prepare-firmware", fu_uefi_dbx_prepare_firmware_func);
	g_test_add_func("/uefi-dbx/validate-filename", fu_uefi_dbx_validate_filename_func);
	return g_test_run();
}
