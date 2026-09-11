/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCAL_MOC_TRANSPORT_IMPL (fu_focal_moc_transport_impl_get_type())
G_DECLARE_INTERFACE(FuFocalMocTransportImpl,
		    fu_focal_moc_transport_impl,
		    FU,
		    FOCAL_MOC_TRANSPORT_IMPL,
		    GObject)

struct _FuFocalMocTransportImplInterface {
	GTypeInterface g_iface;
	gboolean (*write)(FuFocalMocTransportImpl *self,
			  const guint8 *buf,
			  gsize bufsz,
			  guint timeout_ms,
			  GError **error) G_GNUC_NON_NULL(1);
	GByteArray *(*read)(FuFocalMocTransportImpl *self,
			    guint timeout_ms,
			    GError **error)G_GNUC_NON_NULL(1);
};

gboolean
fu_focal_moc_transport_impl_write(FuFocalMocTransportImpl *self,
				  const guint8 *buf,
				  gsize bufsz,
				  guint timeout_ms,
				  GError **error) G_GNUC_NON_NULL(1);
GByteArray *
fu_focal_moc_transport_impl_read(FuFocalMocTransportImpl *self, guint timeout_ms, GError **error)
    G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
