/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2025 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fwupd-jcat-blob.h"
#include "fwupd-jcat-file.h"
#include "fwupd-jcat-item.h"

#include "fu-jcat-context.h"
#include "fu-jcat-engine.h"
#include "fu-jcat-result.h"

#ifdef HAVE_GNUTLS
#include "fu-jcat-gnutls-pkcs7-engine.h"
#endif
#ifdef HAVE_LIBCRYPTO
#include "fu-jcat-libcrypto-pkcs7-engine.h"
#endif

typedef FuJcatEngine *(*FuJcatEngineNewFunc)(FuJcatContext *context);

static void
fu_jcat_sha256_engine_func(void)
{
	g_autofree gchar *fn_fail = NULL;
	g_autofree gchar *fn_pass = NULL;
	g_autofree gchar *sig = NULL;
	g_autoptr(GBytes) blob_sig1 = NULL;
	g_autoptr(GBytes) data_fail = NULL;
	g_autoptr(GBytes) data_fwbin = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdJcatBlob) blob_sig2 = NULL;
	g_autoptr(FuJcatContext) context = fu_jcat_context_new();
	g_autoptr(FuJcatEngine) engine = NULL;
	g_autoptr(FuJcatResult) result_fail = NULL;
	g_autoptr(FuJcatResult) result_pass = NULL;
	const gchar *sig_actual =
	    "a948904f2f0f479b8f8197694b30184b0d2ed1c1cd2a1ec0fb85d299a192a447";

	/* set up context */
	fu_jcat_context_allow_blob_kind(context, FWUPD_JCAT_BLOB_KIND_SHA256);

	/* get engine */
	engine = fu_jcat_context_get_engine(context, FWUPD_JCAT_BLOB_KIND_SHA256, &error);
	g_assert_no_error(error);
	g_assert_nonnull(engine);
	g_assert_cmpint(fu_jcat_engine_get_kind(engine), ==, FWUPD_JCAT_BLOB_KIND_SHA256);
	g_assert_cmpint(fu_jcat_engine_get_method(engine), ==, FWUPD_JCAT_BLOB_METHOD_CHECKSUM);

	/* verify checksum */
	fn_pass = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin", NULL);
	data_fwbin = fu_bytes_get_contents(fn_pass, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fwbin);
	blob_sig1 = g_bytes_new_static(sig_actual, strlen(sig_actual));
	result_pass = fu_jcat_engine_self_verify(engine,
						 data_fwbin,
						 blob_sig1,
						 FU_JCAT_VERIFY_FLAG_NONE,
						 &error);
	g_assert_no_error(error);
	g_assert_nonnull(result_pass);
	g_assert_cmpint(fu_jcat_result_get_timestamp(result_pass), ==, 0);
	g_assert_cmpstr(fu_jcat_result_get_authority(result_pass), ==, NULL);

	/* verify will fail */
	fn_fail = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin.asc", NULL);
	data_fail = fu_bytes_get_contents(fn_fail, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fail);
	result_fail = fu_jcat_engine_self_verify(engine,
						 data_fail,
						 blob_sig1,
						 FU_JCAT_VERIFY_FLAG_NONE,
						 &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(result_fail);
	g_clear_error(&error);

	/* verify signing */
	blob_sig2 = fu_jcat_engine_self_sign(engine, data_fwbin, FU_JCAT_SIGN_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_sig2);
	sig = fwupd_jcat_blob_get_data_as_string(blob_sig2);
	g_assert_cmpstr(sig, ==, sig_actual);
}

static void
fu_jcat_sha512_engine_func(void)
{
	g_autofree gchar *fn_fail = NULL;
	g_autofree gchar *fn_pass = NULL;
	g_autofree gchar *sig = NULL;
	g_autoptr(GBytes) blob_sig1 = NULL;
	g_autoptr(GBytes) data_fail = NULL;
	g_autoptr(GBytes) data_fwbin = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdJcatBlob) blob_sig2 = NULL;
	g_autoptr(FuJcatContext) context = fu_jcat_context_new();
	g_autoptr(FuJcatEngine) engine = NULL;
	g_autoptr(FuJcatResult) result_fail = NULL;
	g_autoptr(FuJcatResult) result_pass = NULL;
	const gchar *sig_actual =
	    "db3974a97f2407b7cae1ae637c0030687a11913274d578492558e39c16c017de84eacdc8c62fe34ee4e12b"
	    "4b1428817f09b6a2760c3f8a664ceae94d2434a593";

	/* set up context */
	fu_jcat_context_allow_blob_kind(context, FWUPD_JCAT_BLOB_KIND_SHA512);

	/* get engine */
	engine = fu_jcat_context_get_engine(context, FWUPD_JCAT_BLOB_KIND_SHA512, &error);
	g_assert_no_error(error);
	g_assert_nonnull(engine);
	g_assert_cmpint(fu_jcat_engine_get_kind(engine), ==, FWUPD_JCAT_BLOB_KIND_SHA512);
	g_assert_cmpint(fu_jcat_engine_get_method(engine), ==, FWUPD_JCAT_BLOB_METHOD_CHECKSUM);

	/* verify checksum */
	fn_pass = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin", NULL);
	data_fwbin = fu_bytes_get_contents(fn_pass, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fwbin);
	blob_sig1 = g_bytes_new_static(sig_actual, strlen(sig_actual));
	result_pass = fu_jcat_engine_self_verify(engine,
						 data_fwbin,
						 blob_sig1,
						 FU_JCAT_VERIFY_FLAG_NONE,
						 &error);
	g_assert_no_error(error);
	g_assert_nonnull(result_pass);
	g_assert_cmpint(fu_jcat_result_get_timestamp(result_pass), ==, 0);
	g_assert_cmpstr(fu_jcat_result_get_authority(result_pass), ==, NULL);

	/* verify will fail */
	fn_fail = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin.asc", NULL);
	data_fail = fu_bytes_get_contents(fn_fail, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fail);
	result_fail = fu_jcat_engine_self_verify(engine,
						 data_fail,
						 blob_sig1,
						 FU_JCAT_VERIFY_FLAG_NONE,
						 &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(result_fail);
	g_clear_error(&error);

	/* verify signing */
	blob_sig2 = fu_jcat_engine_self_sign(engine, data_fwbin, FU_JCAT_SIGN_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_sig2);
	sig = fwupd_jcat_blob_get_data_as_string(blob_sig2);
	g_assert_cmpstr(sig, ==, sig_actual);
}

static void
fwupd_jcat_pkcs7_engine_func(gconstpointer test_data)
{
	g_autofree gchar *fn_fail = NULL;
	g_autofree gchar *fn_pass = NULL;
	g_autofree gchar *fn_sig = NULL;
	g_autofree gchar *pki_dir = NULL;
	g_autofree gchar *sig_fn2 = NULL;
	g_autoptr(GBytes) blob_sig2 = NULL;
	g_autoptr(GBytes) data_fail = NULL;
	g_autoptr(GBytes) data_fwbin = NULL;
	g_autoptr(GBytes) data_sig = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuJcatContext) context = fu_jcat_context_new();
	g_autoptr(FuJcatEngine) engine = NULL;
	g_autoptr(FuJcatResult) result_fail = NULL;
	g_autoptr(FuJcatResult) result_pass = NULL;

	/* set up context */
	pki_dir = g_test_build_filename(G_TEST_DIST, "tests", "pki", NULL);
	fu_jcat_context_add_public_keys(context, pki_dir);
	fu_jcat_context_allow_blob_kind(context, FWUPD_JCAT_BLOB_KIND_PKCS7);

	/* get engine */
	if (test_data == NULL) {
		engine = fu_jcat_context_get_engine(context, FWUPD_JCAT_BLOB_KIND_PKCS7, &error);
	} else {
		engine = ((FuJcatEngineNewFunc)test_data)(context);
	}
	g_assert_no_error(error);
	g_assert_nonnull(engine);
	g_assert_cmpint(fu_jcat_engine_get_kind(engine), ==, FWUPD_JCAT_BLOB_KIND_PKCS7);
	g_assert_cmpint(fu_jcat_engine_get_method(engine), ==, FWUPD_JCAT_BLOB_METHOD_SIGNATURE);

	/* verify with a signature from the old LVFS */
	fn_pass = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin", NULL);
	data_fwbin = fu_bytes_get_contents(fn_pass, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fwbin);
	fn_sig = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin.p7b", NULL);
	data_sig = fu_bytes_get_contents(fn_sig, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_sig);
	result_pass = fu_jcat_engine_pubkey_verify(engine,
						   data_fwbin,
						   data_sig,
						   FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS,
						   &error);
	g_assert_no_error(error);
	g_assert_nonnull(result_pass);
	g_assert_cmpint(fu_jcat_result_get_timestamp(result_pass), >=, 1502871248);
	g_assert_cmpstr(fu_jcat_result_get_authority(result_pass),
			==,
			"O=Linux Vendor Firmware Project,CN=LVFS CA");

	/* verify will fail with a self-signed signature */
	sig_fn2 =
	    g_test_build_filename(G_TEST_BUILT, "tests", "colorhug", "firmware.bin.p7c", NULL);
	blob_sig2 = fu_bytes_get_contents(sig_fn2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_sig2);
	result_fail = fu_jcat_engine_pubkey_verify(engine,
						   data_fwbin,
						   blob_sig2,
						   FU_JCAT_VERIFY_FLAG_NONE,
						   &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(result_fail);
	g_clear_error(&error);

	/* verify will fail with valid signature and different data */
	fn_fail = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin.asc", NULL);
	data_fail = fu_bytes_get_contents(fn_fail, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fail);
	result_fail = fu_jcat_engine_pubkey_verify(engine,
						   data_fail,
						   data_sig,
						   FU_JCAT_VERIFY_FLAG_NONE,
						   &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(result_fail);
	g_clear_error(&error);
}

static void
fwupd_jcat_pkcs7_engine_self_signed_func(gconstpointer test_data)
{
	static const char payload_str[] = "{\n  \"hello\": \"world\"\n}";
	static const char payload_other_str[] = "{\n  \"hello\": \"xorld\"\n}";
	g_autofree gchar *other_keyring_path = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdJcatBlob) signature = NULL;
	g_autoptr(FwupdJcatBlob) signature_fresh = NULL;
	g_autoptr(FuJcatContext) context = fu_jcat_context_new();
	g_autoptr(FuJcatEngine) engine = NULL;
	g_autoptr(FuJcatResult) result = NULL;
	g_autoptr(FuJcatResult) result_fail = NULL;
	g_autoptr(GBytes) payload = NULL;
	g_autoptr(GBytes) payload_other = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *str_perfect = "FuJcatResult:\n"
				   "    Timestamp:          1970-01-01T03:25:45Z\n"
				   "    FuJcat*Pkcs7Engine:\n"
				   "        Kind:           pkcs7\n"
				   "        VerifyKind:     signature\n";

	/* set up context */
	fu_jcat_context_allow_blob_kind(context, FWUPD_JCAT_BLOB_KIND_PKCS7);

	/* get engine */
	if (test_data == NULL) {
		engine = fu_jcat_context_get_engine(context, FWUPD_JCAT_BLOB_KIND_PKCS7, &error);
	} else {
		engine = ((FuJcatEngineNewFunc)test_data)(context);
	}
	g_assert_no_error(error);
	g_assert_nonnull(engine);

	payload = g_bytes_new_static(payload_str, sizeof(payload_str));
	g_assert_nonnull(payload);
	signature =
	    fu_jcat_engine_self_sign(engine, payload, FU_JCAT_SIGN_FLAG_ADD_TIMESTAMP, &error);
	g_assert_no_error(error);
	g_assert_nonnull(signature);
	result = fu_jcat_engine_self_verify(engine,
					    payload,
					    fwupd_jcat_blob_get_data(signature),
					    FU_JCAT_VERIFY_FLAG_NONE,
					    &error);
	g_assert_no_error(error);
	g_assert_nonnull(result);

	/* to string */
	g_object_set(result, "timestamp", (gint64)12345, NULL);
	str = fwupd_codec_to_string(FWUPD_CODEC(result));
	g_debug("%s", str);
	g_assert_true(g_pattern_match_simple(str_perfect, str));

	/* verify a payload with the wrong signature */
	payload_other = g_bytes_new_static(payload_other_str, sizeof(payload_other_str));
	result_fail = fu_jcat_engine_self_verify(engine,
						 payload_other,
						 fwupd_jcat_blob_get_data(signature),
						 FU_JCAT_VERIFY_FLAG_NONE,
						 &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(result_fail);
	g_clear_error(&error);

	/* change keyring path */
	other_keyring_path = g_dir_make_tmp("keyring_2_XXXXXX", &error);
	g_assert_no_error(error);
	fu_jcat_context_set_keyring_path(context, other_keyring_path);

	/* generate fresh self signing keys */
	signature_fresh =
	    fu_jcat_engine_self_sign(engine, payload, FU_JCAT_SIGN_FLAG_ADD_TIMESTAMP, &error);
	g_assert_no_error(error);
	g_assert_nonnull(signature_fresh);

	/* verify original signature with original payload but new signature */
	result_fail = fu_jcat_engine_self_verify(engine,
						 payload,
						 fwupd_jcat_blob_get_data(signature),
						 FU_JCAT_VERIFY_FLAG_NONE,
						 &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(result_fail);
	g_clear_error(&error);
}

#ifdef HAVE_GNUTLS_PQC
static void
fwupd_jcat_pkcs7_engine_self_signed_pq_func(gconstpointer test_data)
{
	static const char payload_str[] = "Hello, world!";
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdJcatBlob) signature = NULL;
	g_autoptr(FuJcatContext) context = fu_jcat_context_new();
	g_autoptr(FuJcatEngine) engine = NULL;
	g_autoptr(FuJcatResult) result = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) payload = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *str_perfect = "FuJcatResult:\n"
				   "    Timestamp:          1970-01-01T03:25:45Z\n"
				   "    FuJcatGnutlsPkcs7Engine:\n"
				   "        Kind:           pkcs7\n"
				   "        VerifyKind:     signature\n";

	/* deleted on error */
	tmpdir = fu_temporary_directory_new("pkcs7-engine-self-signed-pq", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	/* set up context */
	fu_jcat_context_set_keyring_path(context, fu_temporary_directory_get_path(tmpdir));

	/* get engine */
	if (test_data == NULL) {
		engine = fu_jcat_context_get_engine(context, FWUPD_JCAT_BLOB_KIND_PKCS7, &error);
	} else {
		engine = ((FuJcatEngineNewFunc)test_data)(context);
	}
	g_assert_no_error(error);
	g_assert_nonnull(engine);

	payload = g_bytes_new_static(payload_str, sizeof(payload_str));
	g_assert_nonnull(payload);
	signature =
	    fu_jcat_engine_self_sign(engine,
				     payload,
				     FU_JCAT_SIGN_FLAG_ADD_TIMESTAMP | FU_JCAT_SIGN_FLAG_USE_PQ,
				     &error);
	if (signature == NULL && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("ML-DSA cannot be enabled at runtime, skipping");
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(signature);
	result = fu_jcat_engine_self_verify(engine,
					    payload,
					    fwupd_jcat_blob_get_data(signature),
					    FU_JCAT_VERIFY_FLAG_ONLY_PQ,
					    &error);
	if (result == NULL && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("ML-DSA cannot be enabled at runtime, skipping");
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(result);

	/* to string */
	g_object_set(result, "timestamp", (gint64)12345, NULL);
	str = fwupd_codec_to_string(FWUPD_CODEC(result));
	g_debug("%s", str);
	g_assert_cmpstr(str, ==, str_perfect);
}
#endif

static void
fu_jcat_context_verify_blob_func(void)
{
	g_autofree gchar *fn_pass = NULL;
	g_autofree gchar *fn_sig = NULL;
	g_autofree gchar *pki_dir = NULL;
	g_autoptr(GBytes) data_fwbin = NULL;
	g_autoptr(GBytes) data_sig = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdJcatBlob) blob = NULL;
	g_autoptr(FuJcatContext) context = fu_jcat_context_new();
	g_autoptr(FuJcatEngine) engine1 = NULL;
	g_autoptr(FuJcatEngine) engine3 = NULL;
	g_autoptr(FuJcatEngine) engine4 = NULL;
	g_autoptr(FuJcatResult) result = NULL;

	/* set up context */
	pki_dir = g_test_build_filename(G_TEST_DIST, "tests", "pki", NULL);
	fu_jcat_context_add_public_keys(context, pki_dir);
	fu_jcat_context_allow_blob_kind(context, FWUPD_JCAT_BLOB_KIND_PKCS7);
	fu_jcat_context_allow_blob_kind(context, FWUPD_JCAT_BLOB_KIND_SHA256);

	/* get all engines */
	engine1 = fu_jcat_context_get_engine(context, FWUPD_JCAT_BLOB_KIND_SHA256, &error);
	g_assert_no_error(error);
	g_assert_nonnull(engine1);
	engine3 = fu_jcat_context_get_engine(context, FWUPD_JCAT_BLOB_KIND_PKCS7, &error);
	g_assert_no_error(error);
	g_assert_nonnull(engine3);
	engine4 = fu_jcat_context_get_engine(context, FWUPD_JCAT_BLOB_KIND_GPG, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(engine4);
	g_clear_error(&error);

	/* verify blob */
	fn_pass = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin", NULL);
	data_fwbin = fu_bytes_get_contents(fn_pass, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fwbin);
	fn_sig = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin.p7b", NULL);
	data_sig = fu_bytes_get_contents(fn_sig, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_sig);
	blob = fwupd_jcat_blob_new(FWUPD_JCAT_BLOB_KIND_PKCS7, data_sig, FWUPD_JCAT_BLOB_FLAG_NONE);
	result = fu_jcat_context_verify_blob(context,
					     data_fwbin,
					     blob,
					     FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS,
					     &error);
	g_assert_no_error(error);
	g_assert_nonnull(result);
	g_assert_cmpint(fu_jcat_result_get_timestamp(result), >=, 1502871248);
	g_assert_cmpstr(fu_jcat_result_get_authority(result),
			==,
			"O=Linux Vendor Firmware Project,CN=LVFS CA");
}

static void
fu_jcat_context_verify_blob_disallow_func(void)
{
	g_autofree gchar *fn_pass = NULL;
	g_autofree gchar *fn_sig = NULL;
	g_autofree gchar *pki_dir = NULL;
	g_autoptr(GBytes) data_fwbin = NULL;
	g_autoptr(GBytes) data_sig = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdJcatBlob) blob = NULL;
	g_autoptr(FuJcatContext) context = fu_jcat_context_new();
	g_autoptr(FuJcatResult) result = NULL;

	/* set up context (no PKCS#7 support) */
	pki_dir = g_test_build_filename(G_TEST_DIST, "tests", "pki", NULL);
	fu_jcat_context_add_public_keys(context, pki_dir);

	/* verify blob */
	fn_pass = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin", NULL);
	data_fwbin = fu_bytes_get_contents(fn_pass, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fwbin);
	fn_sig = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin.p7b", NULL);
	data_sig = fu_bytes_get_contents(fn_sig, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_sig);
	blob = fwupd_jcat_blob_new(FWUPD_JCAT_BLOB_KIND_PKCS7, data_sig, FWUPD_JCAT_BLOB_FLAG_NONE);

	/* not supported */
	result = fu_jcat_context_verify_blob(context,
					     data_fwbin,
					     blob,
					     FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS,
					     &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(result);
}

static void
fu_jcat_context_verify_item_sign_func(void)
{
	FuJcatResult *result;
	g_autofree gchar *fn_pass = NULL;
	g_autofree gchar *fn_sig = NULL;
	g_autofree gchar *pki_dir = NULL;
	g_autoptr(GBytes) data_fwbin = NULL;
	g_autoptr(GBytes) data_sig = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdJcatBlob) blob = NULL;
	g_autoptr(FwupdJcatItem) item = fwupd_jcat_item_new("filename.bin");
	g_autoptr(FuJcatContext) context = fu_jcat_context_new();
	g_autoptr(FuJcatEngine) engine1 = NULL;
	g_autoptr(FuJcatEngine) engine3 = NULL;
	g_autoptr(GPtrArray) results_fail = NULL;
	g_autoptr(GPtrArray) results_pass = NULL;

	/* set up context */
	pki_dir = g_test_build_filename(G_TEST_DIST, "tests", "pki", NULL);
	fu_jcat_context_add_public_keys(context, pki_dir);
	fu_jcat_context_allow_blob_kind(context, FWUPD_JCAT_BLOB_KIND_PKCS7);
	fu_jcat_context_allow_blob_kind(context, FWUPD_JCAT_BLOB_KIND_SHA256);

	/* get all engines */
	engine1 = fu_jcat_context_get_engine(context, FWUPD_JCAT_BLOB_KIND_SHA256, &error);
	g_assert_no_error(error);
	g_assert_nonnull(engine1);
	engine3 = fu_jcat_context_get_engine(context, FWUPD_JCAT_BLOB_KIND_PKCS7, &error);
	g_assert_no_error(error);
	g_assert_nonnull(engine3);

	/* verify blob */
	fn_pass = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin", NULL);
	data_fwbin = fu_bytes_get_contents(fn_pass, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fwbin);
	fn_sig = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin.p7b", NULL);
	data_sig = fu_bytes_get_contents(fn_sig, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_sig);
	blob = fwupd_jcat_blob_new(FWUPD_JCAT_BLOB_KIND_PKCS7, data_sig, FWUPD_JCAT_BLOB_FLAG_NONE);
	fwupd_jcat_item_add_blob(item, blob);
	results_pass = fu_jcat_context_verify_item(context,
						   data_fwbin,
						   item,
						   FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS |
						       FU_JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE,
						   &error);
	g_assert_no_error(error);
	g_assert_nonnull(results_pass);
	g_assert_cmpint(results_pass->len, ==, 1);
	result = g_ptr_array_index(results_pass, 0);
	g_assert_cmpint(fu_jcat_result_get_timestamp(result), >=, 1502871248);
	g_assert_cmpstr(fu_jcat_result_get_authority(result),
			==,
			"O=Linux Vendor Firmware Project,CN=LVFS CA");

	/* enforce a checksum */
	results_fail = fu_jcat_context_verify_item(context,
						   data_fwbin,
						   item,
						   FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS |
						       FU_JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM,
						   &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(results_fail);
	g_clear_error(&error);
}

static void
fu_jcat_context_verify_item_target_func(void)
{
	FuJcatResult *result;
	g_autofree gchar *fn_pass = NULL;
	g_autofree gchar *fn_sig = NULL;
	g_autofree gchar *pki_dir = NULL;
	g_autoptr(GBytes) data_fwbin = NULL;
	g_autoptr(GBytes) data_sig = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdJcatBlob) blob_pkcs7 = NULL;
	g_autoptr(FwupdJcatBlob) blob_target_sha256 = NULL;
	g_autoptr(FwupdJcatItem) item = fwupd_jcat_item_new("filename.bin");
	g_autoptr(FwupdJcatItem) item_target = fwupd_jcat_item_new("filename.bin");
	g_autoptr(FuJcatContext) context = fu_jcat_context_new();
	g_autoptr(FuJcatEngine) engine_sha256 = NULL;
	g_autoptr(GPtrArray) results_fail = NULL;
	g_autoptr(GPtrArray) results_pass = NULL;

	/* set up context */
	pki_dir = g_test_build_filename(G_TEST_BUILT, "tests", "pki", NULL);
	fu_jcat_context_add_public_keys(context, pki_dir);
	fu_jcat_context_allow_blob_kind(context, FWUPD_JCAT_BLOB_KIND_PKCS7);
	fu_jcat_context_allow_blob_kind(context, FWUPD_JCAT_BLOB_KIND_SHA256);

	/* get all engines */
	engine_sha256 = fu_jcat_context_get_engine(context, FWUPD_JCAT_BLOB_KIND_SHA256, &error);
	g_assert_no_error(error);
	g_assert_nonnull(engine_sha256);

	/* add SHA256 hash as a target blob */
	fn_pass = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin", NULL);
	data_fwbin = fu_bytes_get_contents(fn_pass, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fwbin);
	blob_target_sha256 =
	    fu_jcat_engine_self_sign(engine_sha256, data_fwbin, FU_JCAT_SIGN_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_target_sha256);
	fwupd_jcat_item_add_blob(item_target, blob_target_sha256);

	/* create the item to verify, with a checksum and the PKCS#7 signature *of the hash* */
	fwupd_jcat_item_add_blob(item, blob_target_sha256);
	fn_sig = g_test_build_filename(G_TEST_BUILT,
				       "tests",
				       "colorhug",
				       "firmware.bin.sha256.p7c",
				       NULL);
	data_sig = fu_bytes_get_contents(fn_sig, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_sig);
	blob_pkcs7 =
	    fwupd_jcat_blob_new(FWUPD_JCAT_BLOB_KIND_PKCS7, data_sig, FWUPD_JCAT_BLOB_FLAG_IS_UTF8);
	fwupd_jcat_blob_set_target(blob_pkcs7, FWUPD_JCAT_BLOB_KIND_SHA256);
	fwupd_jcat_item_add_blob(item, blob_pkcs7);

	/* enforce a checksum and signature match */
	results_pass = fu_jcat_context_verify_target(context,
						     item_target,
						     item,
						     FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS |
							 FU_JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM |
							 FU_JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE,
						     &error);
	g_assert_no_error(error);
	g_assert_nonnull(results_pass);
	g_assert_cmpint(results_pass->len, ==, 2);
	result = g_ptr_array_index(results_pass, 1);
	g_assert_cmpint(fu_jcat_result_get_timestamp(result), >=, 1502871248);
	g_assert_cmpstr(fu_jcat_result_get_authority(result), ==, "O=Hughski Limited");
}

static void
fu_jcat_context_verify_item_csum_func(void)
{
	FuJcatResult *result;
	g_autofree gchar *fn_pass = NULL;
	g_autofree gchar *pki_dir = NULL;
	g_autoptr(GBytes) data_fwbin = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdJcatBlob) blob = NULL;
	g_autoptr(FwupdJcatItem) item = fwupd_jcat_item_new("filename.bin");
	g_autoptr(FuJcatContext) context = fu_jcat_context_new();
	g_autoptr(FuJcatEngine) engine1 = NULL;
	g_autoptr(FuJcatEngine) engine3 = NULL;
	g_autoptr(FuJcatEngine) engine4 = NULL;
	g_autoptr(GPtrArray) results_fail = NULL;
	g_autoptr(GPtrArray) results_pass = NULL;
	const gchar *sig_actual =
	    "a948904f2f0f479b8f8197694b30184b0d2ed1c1cd2a1ec0fb85d299a192a447";

	/* set up context */
	pki_dir = g_test_build_filename(G_TEST_DIST, "tests", "pki", NULL);
	fu_jcat_context_add_public_keys(context, pki_dir);
	fu_jcat_context_allow_blob_kind(context, FWUPD_JCAT_BLOB_KIND_PKCS7);
	fu_jcat_context_allow_blob_kind(context, FWUPD_JCAT_BLOB_KIND_SHA256);

	/* get all engines */
	engine1 = fu_jcat_context_get_engine(context, FWUPD_JCAT_BLOB_KIND_SHA256, &error);
	g_assert_no_error(error);
	g_assert_nonnull(engine1);
	engine3 = fu_jcat_context_get_engine(context, FWUPD_JCAT_BLOB_KIND_PKCS7, &error);
	g_assert_no_error(error);
	g_assert_nonnull(engine3);

	/* verify blob */
	fn_pass = g_test_build_filename(G_TEST_DIST, "tests", "colorhug", "firmware.bin", NULL);
	data_fwbin = fu_bytes_get_contents(fn_pass, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fwbin);
	blob = fwupd_jcat_blob_new_utf8(FWUPD_JCAT_BLOB_KIND_SHA256, sig_actual);
	fwupd_jcat_item_add_blob(item, blob);
	results_pass = fu_jcat_context_verify_item(context,
						   data_fwbin,
						   item,
						   FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS |
						       FU_JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM,
						   &error);
	g_assert_no_error(error);
	g_assert_nonnull(results_pass);
	g_assert_cmpint(results_pass->len, ==, 1);
	result = g_ptr_array_index(results_pass, 0);
	g_assert_cmpint(fu_jcat_result_get_timestamp(result), ==, 0);
	g_assert_cmpstr(fu_jcat_result_get_authority(result), ==, NULL);

	/* enforce a signature */
	results_fail = fu_jcat_context_verify_item(context,
						   data_fwbin,
						   item,
						   FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS |
						       FU_JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE,
						   &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(results_fail);
	g_clear_error(&error);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);

	/* ISOLATE_DIRS allows us to avoid creating keyring paths for each test */
	g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	g_test_add_func("/jcat/engine/sha256", fu_jcat_sha256_engine_func);
	g_test_add_func("/jcat/engine/sha512", fu_jcat_sha512_engine_func);
	g_test_add_data_func("/jcat/engine/pkcs7", NULL, fwupd_jcat_pkcs7_engine_func);
	g_test_add_data_func("/jcat/engine/pkcs7/self-signed",
			     NULL,
			     fwupd_jcat_pkcs7_engine_self_signed_func);
#ifdef HAVE_LIBCRYPTO
	g_test_add_data_func("/jcat/engine/pkcs7/openssl",
			     &fu_jcat_libcrypto_pkcs7_engine_new,
			     fwupd_jcat_pkcs7_engine_func);
	g_test_add_data_func("/jcat/engine/pkcs7/self-signed/openssl",
			     &fu_jcat_libcrypto_pkcs7_engine_new,
			     fwupd_jcat_pkcs7_engine_self_signed_func);
#endif
#ifdef HAVE_GNUTLS
	g_test_add_data_func("/jcat/engine/pkcs7/gnutls",
			     &fu_jcat_gnutls_pkcs7_engine_new,
			     fwupd_jcat_pkcs7_engine_func);
	g_test_add_data_func("/jcat/engine/pkcs7/self-signed/gnutls",
			     &fu_jcat_gnutls_pkcs7_engine_new,
			     fwupd_jcat_pkcs7_engine_self_signed_func);
#endif
#ifdef HAVE_GNUTLS_PQC
	g_test_add_data_func("/jcat/engine/pkcs7-self-signed/pq-gnutls",
			     &fu_jcat_gnutls_pkcs7_engine_new,
			     fwupd_jcat_pkcs7_engine_self_signed_pq_func);
#endif
	g_test_add_func("/jcat/context/verify/blob", fu_jcat_context_verify_blob_func);
	g_test_add_func("/jcat/context/verify/blob/disallow",
			fu_jcat_context_verify_blob_disallow_func);
	g_test_add_func("/jcat/context/verify/item/sign", fu_jcat_context_verify_item_sign_func);
	g_test_add_func("/jcat/context/verify/item/csum", fu_jcat_context_verify_item_csum_func);
	g_test_add_func("/jcat/context/verify/item/target",
			fu_jcat_context_verify_item_target_func);
	return g_test_run();
}
