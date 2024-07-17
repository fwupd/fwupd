/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <json-glib/json-glib.h>

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
	FuDevice *(*get_device_parent)(FuBackend *self,
				       FuDevice *device,
				       const gchar *kind,
				       GError **error)G_GNUC_WARN_UNUSED_RESULT;
};

const gchar *
fu_backend_get_name(FuBackend *self) G_GNUC_NON_NULL(1);
FuContext *
fu_backend_get_context(FuBackend *self) G_GNUC_NON_NULL(1);
gboolean
fu_backend_get_enabled(FuBackend *self) G_GNUC_NON_NULL(1);
void
fu_backend_set_enabled(FuBackend *self, gboolean enabled) G_GNUC_NON_NULL(1);
GPtrArray *
fu_backend_get_devices(FuBackend *self) G_GNUC_NON_NULL(1);
FuDevice *
fu_backend_lookup_by_id(FuBackend *self, const gchar *backend_id) G_GNUC_NON_NULL(1, 2);
gboolean
fu_backend_setup(FuBackend *self, FuProgress *progress, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
gboolean
fu_backend_coldplug(FuBackend *self, FuProgress *progress, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
void
fu_backend_device_added(FuBackend *self, FuDevice *device) G_GNUC_NON_NULL(1, 2);
void
fu_backend_device_removed(FuBackend *self, FuDevice *device) G_GNUC_NON_NULL(1, 2);
void
fu_backend_device_changed(FuBackend *self, FuDevice *device) G_GNUC_NON_NULL(1, 2);
void
fu_backend_registered(FuBackend *self, FuDevice *device) G_GNUC_NON_NULL(1, 2);
void
fu_backend_invalidate(FuBackend *self) G_GNUC_NON_NULL(1);
void
fu_backend_add_string(FuBackend *self, guint idt, GString *str) G_GNUC_NON_NULL(1, 3);
FuDevice *
fu_backend_get_device_parent(FuBackend *self, FuDevice *device, const gchar *kind, GError **error);
