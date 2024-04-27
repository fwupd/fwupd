/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_QC_S5GEN2_IMPL (fu_qc_s5gen2_impl_get_type())
G_DECLARE_INTERFACE(FuQcS5gen2Impl, fu_qc_s5gen2_impl, FU, QC_S5GEN2_IMPL, GObject)

struct _FuQcS5gen2ImplInterface {
	GTypeInterface g_iface;
	gboolean (*msg_in)(FuQcS5gen2Impl *self,
			   guint8 *data,
			   gsize data_len,
			   gsize *read_len,
			   GError **error);
	gboolean (*msg_out)(FuQcS5gen2Impl *self, guint8 *data, gsize data_len, GError **error);
	gchar *(*get_version)(FuQcS5gen2Impl *self, GError **error);
	gboolean (*req_connect)(FuQcS5gen2Impl *self, GError **error);
	gboolean (*req_disconnect)(FuQcS5gen2Impl *self, GError **error);
	gboolean (*data_size)(FuQcS5gen2Impl *self, gsize *datasz, GError **error);
};

gboolean
fu_qc_s5gen2_impl_msg_in(FuQcS5gen2Impl *self,
			 guint8 *data,
			 gsize data_len,
			 gsize *read_len,
			 GError **error);
gboolean
fu_qc_s5gen2_impl_msg_out(FuQcS5gen2Impl *self, guint8 *data, gsize data_len, GError **error);
gboolean
fu_qc_s5gen2_impl_req_connect(FuQcS5gen2Impl *self, GError **error);
gboolean
fu_qc_s5gen2_impl_req_disconnect(FuQcS5gen2Impl *self, GError **error);
gboolean
fu_qc_s5gen2_impl_data_size(FuQcS5gen2Impl *self, gsize *data_sz, GError **error);
