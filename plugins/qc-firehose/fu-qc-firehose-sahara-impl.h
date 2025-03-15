/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_QC_FIREHOSE_SAHARA_IMPL (fu_qc_firehose_sahara_impl_get_type())
G_DECLARE_INTERFACE(FuQcFirehoseSaharaImpl,
		    fu_qc_firehose_sahara_impl,
		    FU,
		    QC_FIREHOSE_SAHARA_IMPL,
		    GObject)

struct _FuQcFirehoseSaharaImplInterface {
	GTypeInterface g_iface;
	GByteArray *(*read)(FuQcFirehoseSaharaImpl *self,
			    guint timeout_ms,
			    GError **error)G_GNUC_NON_NULL(1);
	gboolean (*write)(FuQcFirehoseSaharaImpl *self,
			  const guint8 *buf,
			  gsize bufsz,
			  GError **error) G_GNUC_NON_NULL(1);
};

gboolean
fu_qc_firehose_sahara_impl_write_firmware(FuQcFirehoseSaharaImpl *self,
					  FuFirmware *firmware,
					  FuProgress *progress,
					  GError **error) G_GNUC_NON_NULL(1, 2, 3);
