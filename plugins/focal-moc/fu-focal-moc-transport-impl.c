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

gboolean
fu_focal_moc_transport_impl_get_host_key_journal(FuFocalMocTransportImpl *self,
						 gboolean *available,
						 GError **error)
{
	FuFocalMocTransportImplInterface *iface;

	g_return_val_if_fail(FU_IS_FOCAL_MOC_TRANSPORT_IMPL(self), FALSE);
	g_return_val_if_fail(available != NULL, FALSE);

	iface = FU_FOCAL_MOC_TRANSPORT_IMPL_GET_IFACE(self);
	if ((iface->load_host_key != NULL) != (iface->save_host_key != NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "host key journal requires both load and save");
		return FALSE;
	}
	*available = iface->load_host_key != NULL;
	return TRUE;
}

gboolean
fu_focal_moc_transport_impl_load_host_key(FuFocalMocTransportImpl *self,
					  guint8 *host_key,
					  gboolean *found,
					  GError **error)
{
	FuFocalMocTransportImplInterface *iface;

	g_return_val_if_fail(FU_IS_FOCAL_MOC_TRANSPORT_IMPL(self), FALSE);

	iface = FU_FOCAL_MOC_TRANSPORT_IMPL_GET_IFACE(self);
	if (iface->load_host_key == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->load_host_key not implemented");
		return FALSE;
	}
	return (*iface->load_host_key)(self, host_key, found, error);
}

gboolean
fu_focal_moc_transport_impl_save_host_key(FuFocalMocTransportImpl *self,
					  const guint8 *host_key,
					  GError **error)
{
	FuFocalMocTransportImplInterface *iface;

	g_return_val_if_fail(FU_IS_FOCAL_MOC_TRANSPORT_IMPL(self), FALSE);

	iface = FU_FOCAL_MOC_TRANSPORT_IMPL_GET_IFACE(self);
	if (iface->save_host_key == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->save_host_key not implemented");
		return FALSE;
	}
	return (*iface->save_host_key)(self, host_key, error);
}
