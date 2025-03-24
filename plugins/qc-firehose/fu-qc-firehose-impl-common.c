/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-qc-firehose-impl-common.h"

/**
 * fu_qc_firehose_impl_retry:
 * @self: (nullable): a #FuQcFirehoseImpl
 * @timeout_ms: timeout total
 * @func: (scope caller): a #FuQcFirehoseImplRetryFunc
 * @user_data: pointer for @func
 * @error: (nullable): optional return location for an error
 *
 * Retry @func up to 100 times, but if the function keeps replying with "timeout" then this will
 * abort with a failure after @timeout_ms.
 *
 * NOTE: we can't use `GTimer` or `g_usleep()` here as we want to do this in ~0 time when emulating,
 * so keep a counter of the timeout total and assume that @func is limited to 500ms.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_qc_firehose_impl_retry(FuQcFirehoseImpl *self,
			  guint timeout_ms,
			  FuQcFirehoseImplRetryFunc func,
			  gpointer user_data,
			  GError **error)
{
	const guint retry_cnt = 100;
	const guint retry_timeout = 500; /* ms */
	guint total_ms = 0;

	g_return_val_if_fail(func != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* try up to retry_cnt tries, but always less than timeout_ms */
	for (guint i = 0; total_ms < timeout_ms; i++) {
		gboolean done = FALSE;
		g_autoptr(GError) error_local = NULL;

		/* sanity check */
		if (i >= retry_cnt) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "retry limit %u reached: ",
				    retry_cnt);
			return FALSE;
		}
		if (!func(self, &done, retry_timeout, user_data, &error_local)) {
			if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT)) {
				g_propagate_error(error, g_steal_pointer(&error_local));
				return FALSE;
			}
			g_debug("ignoring: %s", error_local->message);
			total_ms += retry_timeout;
		} else if (done) {
			return TRUE;
		}
	}
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT, "timed out after %ums", total_ms);
	return FALSE;
}
