/*
 * Copyright (C) 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-audio-s5gen2-impl.h"

G_DEFINE_INTERFACE(FuQcS5gen2Impl, fu_qc_s5gen2_impl, G_TYPE_OBJECT)

static void
fu_qc_s5gen2_impl_default_init(FuQcS5gen2ImplInterface *iface)
{
}

gboolean
fu_qc_s5gen2_impl_msg_in(FuQcS5gen2Impl *self, guint8 *data, gsize data_len, GError **error)
{
	FuQcS5gen2ImplInterface *iface;

	g_return_val_if_fail(FU_IS_QC_S5GEN2_IMPL(self), FALSE);

	iface = FU_QC_S5GEN2_IMPL_GET_IFACE(self);
	if (iface->msg_in == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->msg_in not implemented");
		return FALSE;
	}
	return (*iface->msg_in)(self, data, data_len, error);
}

gboolean
fu_qc_s5gen2_impl_msg_out(FuQcS5gen2Impl *self, guint8 *data, gsize data_len, GError **error)
{
	FuQcS5gen2ImplInterface *iface;

	g_return_val_if_fail(FU_IS_QC_S5GEN2_IMPL(self), FALSE);

	iface = FU_QC_S5GEN2_IMPL_GET_IFACE(self);
	if (iface->msg_out == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->msg_out not implemented");
		return FALSE;
	}
	return (*iface->msg_out)(self, data, data_len, error);
}

gboolean
fu_qc_s5gen2_impl_msg_cmd(FuQcS5gen2Impl *self, guint8 *data, gsize data_len, GError **error)
{
	FuQcS5gen2ImplInterface *iface;

	g_return_val_if_fail(FU_IS_QC_S5GEN2_IMPL(self), FALSE);

	iface = FU_QC_S5GEN2_IMPL_GET_IFACE(self);
	if (iface->msg_cmd == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->msg_cmd not implemented");
		return FALSE;
	}
	return (*iface->msg_cmd)(self, data, data_len, error);
}
