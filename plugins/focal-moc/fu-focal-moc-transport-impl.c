/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-transport-impl.h"

G_DEFINE_INTERFACE(FuFocalMocTransportImpl, fu_focal_moc_transport_impl, G_TYPE_OBJECT)

static void
fu_focal_moc_transport_impl_default_init(FuFocalMocTransportImplInterface *iface)
{
}

gboolean
fu_focal_moc_transport_impl_write(FuFocalMocTransportImpl *self,
				  const guint8 *buf,
				  gsize bufsz,
				  guint timeout_ms,
				  GError **error)
{
	FuFocalMocTransportImplInterface *iface;

	g_return_val_if_fail(FU_IS_FOCAL_MOC_TRANSPORT_IMPL(self), FALSE);

	iface = FU_FOCAL_MOC_TRANSPORT_IMPL_GET_IFACE(self);
	if (iface->write == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->write not implemented");
		return FALSE;
	}
	return (*iface->write)(self, buf, bufsz, timeout_ms, error);
}

GByteArray *
fu_focal_moc_transport_impl_read(FuFocalMocTransportImpl *self, guint timeout_ms, GError **error)
{
	FuFocalMocTransportImplInterface *iface;

	g_return_val_if_fail(FU_IS_FOCAL_MOC_TRANSPORT_IMPL(self), NULL);

	iface = FU_FOCAL_MOC_TRANSPORT_IMPL_GET_IFACE(self);
	if (iface->read == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->read not implemented");
		return NULL;
	}
	return (*iface->read)(self, timeout_ms, error);
}
