/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-qc-firehose-struct.h"

#define FU_TYPE_QC_FIREHOSE_IMPL (fu_qc_firehose_impl_get_type())
G_DECLARE_INTERFACE(FuQcFirehoseImpl, fu_qc_firehose_impl, FU, QC_FIREHOSE_IMPL, FuDevice)

struct _FuQcFirehoseImplInterface {
	GTypeInterface g_iface;
	GByteArray *(*read)(FuQcFirehoseImpl *self,
			    guint timeout_ms,
			    GError **error)G_GNUC_NON_NULL(1);
	gboolean (*write)(FuQcFirehoseImpl *self, const guint8 *buf, gsize bufsz, GError **error)
	    G_GNUC_NON_NULL(1);
	gboolean (*has_function)(FuQcFirehoseImpl *self, FuQcFirehoseFunctions func)
	    G_GNUC_NON_NULL(1);
	void (*add_function)(FuQcFirehoseImpl *self, FuQcFirehoseFunctions func) G_GNUC_NON_NULL(1);
};

gboolean
fu_qc_firehose_impl_setup(FuQcFirehoseImpl *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_qc_firehose_impl_write_firmware(FuQcFirehoseImpl *self,
				   FuFirmware *firmware,
				   gboolean no_zlp,
				   FuProgress *progress,
				   GError **error) G_GNUC_NON_NULL(1, 2, 4);
gboolean
fu_qc_firehose_impl_reset(FuQcFirehoseImpl *self, GError **error) G_GNUC_NON_NULL(1);
