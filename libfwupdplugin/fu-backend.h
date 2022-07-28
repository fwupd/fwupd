/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-context.h"
#include "fu-device.h"

#define FU_TYPE_BACKEND (fu_backend_get_type())
G_DECLARE_DERIVABLE_TYPE(FuBackend, fu_backend, FU, BACKEND, GObject)

struct _FuBackendClass {
	GObjectClass parent_class;
	/* signals */
	gboolean (*setup)(FuBackend *self,
			  FuProgress *progress,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*coldplug)(FuBackend *self,
			     FuProgress *progress,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT;
	void (*registered)(FuBackend *self, FuDevice *device);
	void (*invalidate)(FuBackend *self);
	void (*to_string)(FuBackend *self, guint indent, GString *str);
	/*< private >*/
	gpointer padding[26];
};

const gchar *
fu_backend_get_name(FuBackend *self);
FuContext *
fu_backend_get_context(FuBackend *self);
gboolean
fu_backend_get_enabled(FuBackend *self);
void
fu_backend_set_enabled(FuBackend *self, gboolean enabled);
GPtrArray *
fu_backend_get_devices(FuBackend *self);
FuDevice *
fu_backend_lookup_by_id(FuBackend *self, const gchar *device_id);
gboolean
fu_backend_setup(FuBackend *self, FuProgress *progress, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_backend_coldplug(FuBackend *self,
		    FuProgress *progress,
		    GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_backend_device_added(FuBackend *self, FuDevice *device);
void
fu_backend_device_removed(FuBackend *self, FuDevice *device);
void
fu_backend_device_changed(FuBackend *self, FuDevice *device);
void
fu_backend_registered(FuBackend *self, FuDevice *device);
void
fu_backend_invalidate(FuBackend *self);
void
fu_backend_add_string(FuBackend *self, guint idt, GString *str);
