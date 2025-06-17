/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-qc-firehose-impl-common.h"

typedef struct {
	guint cnt;
	guint cnt_done;
	FwupdError error_code;
} FooHelper;

static gboolean
fu_qc_firehose_retry_cb(FuQcFirehoseImpl *self,
			gboolean *done,
			guint timeout_ms,
			gpointer user_data,
			GError **error)
{
	FooHelper *helper = (FooHelper *)user_data;
	helper->cnt++;
	if (helper->cnt_done > 0 && helper->cnt == helper->cnt_done)
		*done = TRUE;
	return TRUE;
}

static void
fu_qc_firehose_retry_true_func(void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	FooHelper helper = {0};

	ret = fu_qc_firehose_impl_retry(NULL, 2500, fu_qc_firehose_retry_cb, &helper, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_false(ret);
	g_assert_cmpint(helper.cnt, ==, 100);
}

static void
fu_qc_firehose_retry_done_func(void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	FooHelper helper = {
	    .cnt_done = 10,
	};

	ret = fu_qc_firehose_impl_retry(NULL, 2500, fu_qc_firehose_retry_cb, &helper, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(helper.cnt, ==, 10);
}

static gboolean
fu_qc_firehose_retry_error_cb(FuQcFirehoseImpl *self,
			      gboolean *done,
			      guint timeout_ms,
			      gpointer user_data,
			      GError **error)
{
	FooHelper *helper = (FooHelper *)user_data;
	helper->cnt++;
	g_set_error_literal(error, FWUPD_ERROR, (gint)helper->error_code, "timeout");
	return FALSE;
}

static void
fu_qc_firehose_retry_timeout_func(void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	FooHelper helper = {
	    .error_code = FWUPD_ERROR_TIMED_OUT,
	};

	ret = fu_qc_firehose_impl_retry(NULL, 2500, fu_qc_firehose_retry_error_cb, &helper, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, (gint)helper.error_code);
	g_assert_cmpint(helper.cnt, ==, 5);
}

static void
fu_qc_firehose_retry_invalid_func(void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	FooHelper helper = {
	    .error_code = FWUPD_ERROR_INVALID_DATA,
	};

	ret = fu_qc_firehose_impl_retry(NULL, 2500, fu_qc_firehose_retry_error_cb, &helper, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, (gint)helper.error_code);
	g_assert_cmpint(helper.cnt, ==, 1);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/qc-firehose/retry{true}", fu_qc_firehose_retry_true_func);
	g_test_add_func("/qc-firehose/retry{done}", fu_qc_firehose_retry_done_func);
	g_test_add_func("/qc-firehose/retry{timeout}", fu_qc_firehose_retry_timeout_func);
	g_test_add_func("/qc-firehose/retry{invalid}", fu_qc_firehose_retry_invalid_func);
	return g_test_run();
}
