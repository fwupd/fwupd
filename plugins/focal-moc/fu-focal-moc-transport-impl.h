/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

/* the host ephemeral key packed as x||y||scalar, 32 bytes each */
#define FU_FOCAL_MOC_TRANSPORT_HOST_KEY_SIZE 96

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
	gboolean (*load_host_key)(FuFocalMocTransportImpl *self,
				  guint8 *host_key,
				  gboolean *found,
				  GError **error) G_GNUC_NON_NULL(1);
	gboolean (*save_host_key)(FuFocalMocTransportImpl *self,
				  const guint8 *host_key,
				  GError **error) G_GNUC_NON_NULL(1);
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
gboolean
fu_focal_moc_transport_impl_get_host_key_journal(FuFocalMocTransportImpl *self,
						 gboolean *available,
						 GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_focal_moc_transport_impl_load_host_key(FuFocalMocTransportImpl *self,
					  guint8 *host_key,
					  gboolean *found,
					  GError **error) G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_focal_moc_transport_impl_save_host_key(FuFocalMocTransportImpl *self,
					  const guint8 *host_key,
					  GError **error) G_GNUC_NON_NULL(1, 2);
