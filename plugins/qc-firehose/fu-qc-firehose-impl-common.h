/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-qc-firehose-impl.h"

typedef gboolean (*FuQcFirehoseImplRetryFunc)(FuQcFirehoseImpl *self,
					      gboolean *done,
					      guint timeout_ms,
					      gpointer user_data,
					      GError **error) G_GNUC_WARN_UNUSED_RESULT;

gboolean
fu_qc_firehose_impl_retry(FuQcFirehoseImpl *self,
			  guint timeout_ms,
			  FuQcFirehoseImplRetryFunc func,
			  gpointer user_data,
			  GError **error) G_GNUC_NON_NULL(3);
