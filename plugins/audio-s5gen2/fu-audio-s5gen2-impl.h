/*
 * Copyright (C) 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_QC_S5GEN2_IMPL (fu_qc_s5gen2_impl_get_type())
G_DECLARE_INTERFACE(FuQcS5gen2Impl, fu_qc_s5gen2_impl, FU, QC_S5GEN2_IMPL, GObject)

struct _FuQcS5gen2ImplInterface {
	GTypeInterface g_iface;
	gboolean (*msg_in)(FuQcS5gen2Impl *self, guint8 *data, gsize data_len, GError **error);
	gboolean (*msg_out)(FuQcS5gen2Impl *self, guint8 *data, gsize data_len, GError **error);
	gboolean (*msg_cmd)(FuQcS5gen2Impl *self, guint8 *data, gsize data_len, GError **error);
};

gboolean
fu_qc_s5gen2_impl_msg_in(FuQcS5gen2Impl *self, guint8 *data, gsize data_len, GError **error);
gboolean
fu_qc_s5gen2_impl_msg_out(FuQcS5gen2Impl *self, guint8 *data, gsize data_len, GError **error);
gboolean
fu_qc_s5gen2_impl_msg_cmd(FuQcS5gen2Impl *self, guint8 *data, gsize data_len, GError **error);
