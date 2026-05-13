/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#include "fu-ioctl-struct.h"

#define FU_TYPE_IOCTL (fu_ioctl_get_type())

G_DECLARE_FINAL_TYPE(FuIoctl, fu_ioctl, FU, IOCTL, GObject)

typedef gboolean (*FuIoctlFixupFunc)(FuIoctl *self,
				     gpointer ptr,
				     guint8 *buf,
				     gsize bufsz,
				     GError **error) G_GNUC_WARN_UNUSED_RESULT;

void
fu_ioctl_set_name(FuIoctl *self, const gchar *name) G_GNUC_NON_NULL(1);
void
fu_ioctl_add_key_as_u8(FuIoctl *self, const gchar *key, gsize value) G_GNUC_NON_NULL(1, 2);
void
fu_ioctl_add_key_as_u16(FuIoctl *self, const gchar *key, gsize value) G_GNUC_NON_NULL(1, 2);
void
fu_ioctl_add_mutable_buffer(FuIoctl *self,
			    const gchar *key,
			    guint8 *buf,
			    gsize bufsz,
			    FuIoctlFixupFunc fixup_cb) G_GNUC_NON_NULL(1);
void
fu_ioctl_add_const_buffer(FuIoctl *self,
			  const gchar *key,
			  const guint8 *buf,
			  gsize bufsz,
			  FuIoctlFixupFunc fixup_cb) G_GNUC_NON_NULL(1);
gboolean
fu_ioctl_execute(FuIoctl *self,
		 gulong request,
		 gpointer buf,
		 gsize bufsz,
		 gint *rc,
		 guint timeout,
		 FuIoctlFlags flags,
		 GError **error) G_GNUC_NON_NULL(1);
